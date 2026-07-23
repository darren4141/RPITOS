#include "software_timer.h"

#include "stddef.h"

#include "interrupts.h"
#include "scheduler.h"
#include "semaphore.h"
#include "task.h"

// Intrusive singly-linked lists — head pointers only, no prev/tail. Each
// SoftwareTimer carries its own `next` and a `list_id` tag identifying which
// list it is in. The blocked list is kept sorted by ascending expiry_tick so
// the tick handler can stop at the first timer that has not yet expired.
static SoftwareTimer *blocked_list_head;
static SoftwareTimer *active_list_head;

static Semaphore software_timer_semaphore;
static TaskControlBlock *software_timer_service_tcb;

// Unlink `timer` from the list headed by *head. Returns true if it was found
// and removed. Caller must hold a critical section (or run with IRQs masked).
static bool software_timer_unlink(SoftwareTimer **head, SoftwareTimer *timer)
{
  if (*head == timer) {
    *head = timer->next;
    timer->next = NULL;
    return true;
  }

  SoftwareTimer *iter = *head;
  while (iter != NULL && iter->next != timer) {
    iter = iter->next;
  }

  if (iter == NULL) {
    return false;   // not in this list
  }

  iter->next = timer->next;
  timer->next = NULL;
  return true;
}

// Insert `timer` into the blocked list sorted by ascending expiry_tick (head is
// the soonest to expire). `expiry_tick` must already be set. Caller must hold a
// critical section (or run with IRQs masked).
static void software_timer_add_to_blocked_list(SoftwareTimer *software_timer)
{
  software_timer->list_id = TIMER_LIST_BLOCKED;
  software_timer->next = NULL;

  if ((blocked_list_head == NULL) || (software_timer->expiry_tick < blocked_list_head->expiry_tick)) {
    software_timer->next = blocked_list_head;
    blocked_list_head = software_timer;
    return;
  }

  SoftwareTimer *iter = blocked_list_head;
  while (iter->next != NULL && iter->next->expiry_tick <= software_timer->expiry_tick) {
    iter = iter->next;
  }
  software_timer->next = iter->next;
  iter->next = software_timer;
}

// Take the given (expired) timer off the blocked list and push it onto the
// active list for the service task to process. Caller must hold a critical
// section (or run with IRQs masked). `software_timer` must be in the blocked list.
static void software_timer_pop_to_active(SoftwareTimer *software_timer)
{
  if (!software_timer_unlink(&blocked_list_head, software_timer)) {
    return;
  }

  // Order within the active list does not matter — prepend.
  software_timer->next = active_list_head;
  active_list_head = software_timer;
  software_timer->list_id = TIMER_LIST_ACTIVE;
}

static void software_timer_service_task(void *params)
{
  (void)params;
  while (1) {
    semaphore_take(&software_timer_semaphore, SEMAPHORE_TAKE_BLOCKING);

    // Pull one expired timer off the active list. Each semaphore_give() from
    // the tick corresponds to exactly one pop_to_active, so one take == one
    // timer to service. A stopped/reset timer can leave a stale count behind,
    // so tolerate an empty active list.
    uint32_t cpsr = enter_critical();
    SoftwareTimer *expired = active_list_head;
    if (expired != NULL) {
      active_list_head = expired->next;
      expired->next = NULL;
      expired->list_id = TIMER_LIST_NONE;
    }
    exit_critical(cpsr);

    if (expired == NULL) {
      continue;
    }

    if (expired->callback != NULL) {
      expired->callback(expired);
    }

    // Re-arm periodic timers relative to their scheduled expiry (drift-free).
    if (expired->timer_mode == TIMER_MODE_PERIODIC) {
      cpsr = enter_critical();
      expired->expiry_tick += expired->period;
      software_timer_add_to_blocked_list(expired);
      exit_critical(cpsr);
    }
  }
}

StatusCode software_timer_init()
{
  blocked_list_head = NULL;
  active_list_head = NULL;

  semaphore_init(&software_timer_semaphore, SEMAPHORE_MAX_COUNT_UNLIMITED, 0U);

  return E_OK;
}

StatusCode software_timer_start()
{
  StatusCode ret;

  ret = task_create(software_timer_service_task, 512, TASK_PRIORITY_5, NULL, &software_timer_service_tcb);
  if (ret != E_OK) {
    return ret;
  }

  return E_OK;
}

StatusCode software_timer_create(SoftwareTimer *software_timer, uint64_t period, TimerCallback callback_function, TimerMode timer_mode)
{
  if ((software_timer == NULL) || (period == 0)) {
    return E_INVALID_ARGS;
  }

  software_timer->period = period;
  software_timer->callback = callback_function;
  software_timer->timer_mode = timer_mode;
  software_timer->expiry_tick = 0;
  software_timer->list_id = TIMER_LIST_NONE;
  software_timer->next = NULL;

  // Arm immediately only if the scheduler is already running (i.e. we are being
  // called from a task). If created during boot before schedulerStart(), leave
  // the timer unarmed — the caller starts it later with software_timer_reset().
  if (scheduler_get_current_task() != NULL) {
    uint32_t cpsr = enter_critical();
    software_timer->expiry_tick = scheduler_get_tick_count() + period;
    software_timer_add_to_blocked_list(software_timer);
    exit_critical(cpsr);
  }

  return E_OK;
}

StatusCode software_timer_stop(SoftwareTimer *software_timer)
{
  if (software_timer == NULL) {
    return E_INVALID_ARGS;
  }

  uint32_t cpsr = enter_critical();
  if (software_timer->list_id == TIMER_LIST_BLOCKED) {
    software_timer_unlink(&blocked_list_head, software_timer);
  }
  else if (software_timer->list_id == TIMER_LIST_ACTIVE) {
    software_timer_unlink(&active_list_head, software_timer);
  }
  software_timer->list_id = TIMER_LIST_NONE;
  exit_critical(cpsr);

  return E_OK;
}

StatusCode software_timer_reset(SoftwareTimer *software_timer)
{
  if (software_timer == NULL) {
    return E_INVALID_ARGS;
  }

  uint32_t cpsr = enter_critical();
  if (software_timer->list_id == TIMER_LIST_BLOCKED) {
    software_timer_unlink(&blocked_list_head, software_timer);
  }
  else if (software_timer->list_id == TIMER_LIST_ACTIVE) {
    software_timer_unlink(&active_list_head, software_timer);
  }
  software_timer->expiry_tick = scheduler_get_tick_count() + software_timer->period;
  software_timer_add_to_blocked_list(software_timer);
  exit_critical(cpsr);

  return E_OK;
}

void software_timer_tick(uint64_t now_tick)
{
  // Runs in tick/IRQ context with interrupts already masked. The blocked list is
  // sorted by ascending expiry_tick, so stop at the first non-expired timer.
  while (blocked_list_head != NULL && blocked_list_head->expiry_tick <= now_tick) {
    SoftwareTimer *expired = blocked_list_head;
    software_timer_pop_to_active(expired);
    semaphore_give(&software_timer_semaphore);
  }
}
