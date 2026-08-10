#include "semaphore.h"

#include <stddef.h>

#include "interrupts.h"
#include "scheduler.h"
#include "telemetry.h"
#include "uart.h"

static void semaphore_add_to_blocked_list(Semaphore *smph, TaskControlBlock *tcb)
{
  ListItem *ei = &tcb->event_list_item;
  ei->owner = tcb;
  ei->container = &smph->semaphore_blocked_list;
  ei->next = NULL;
  ei->prev = smph->semaphore_blocked_list.list_end;

  if (smph->semaphore_blocked_list.list_end != NULL) {
    smph->semaphore_blocked_list.list_end->next = ei;
  }
  else {
    smph->semaphore_blocked_list.head = ei;
  }
  smph->semaphore_blocked_list.list_end = ei;
  smph->semaphore_blocked_list.num_items++;
}

// Shared by semaphore_init()/semaphore_init_with_parent() — every field except the
// telemetry registration itself (which needs to know the parent, so stays out of here).
static void semaphore_init_common(Semaphore *smph, uint32_t max_count, uint32_t initial_count)
{
  smph->max_count = max_count;
  smph->count = initial_count;

  smph->semaphore_blocked_list.head = NULL;
  smph->semaphore_blocked_list.index = NULL;
  smph->semaphore_blocked_list.list_end = NULL;
  smph->semaphore_blocked_list.num_items = 0;

  spinlock_init(&smph->lock);
}

void semaphore_init(Semaphore *smph, uint32_t max_count, uint32_t initial_count, const char *name)
{
  semaphore_init_common(smph, max_count, initial_count);
#ifdef RTOS_TELEMETRY
  smph->sync_id = telemetry_register_sync(SYNC_KIND_SEMAPHORE, TELEMETRY_SYNC_ID_NONE, name);
#else
  (void)name;
#endif
}

#ifdef RTOS_TELEMETRY
void semaphore_init_with_parent(Semaphore *smph, uint32_t max_count, uint32_t initial_count, const char *name,
                                 uint16_t parent_sync_id)
{
  semaphore_init_common(smph, max_count, initial_count);
  smph->sync_id = telemetry_register_sync(SYNC_KIND_SEMAPHORE, parent_sync_id, name);
}
#endif

StatusCode semaphore_take(Semaphore *smph, int64_t timeout_ms)
{
  uint64_t deadline = (timeout_ms > 0) ? scheduler_get_tick_count() + (uint64_t)timeout_ms : 0;
  TaskControlBlock *cur_tcb = scheduler_get_current_task();
  uint32_t core_id = cur_tcb->core_id;   // a task only ever takes on its own core

  while (1) {
    uint32_t cpsr = enter_critical();
    spinlock_acquire(&smph->lock);

    if (smph->count > 0) {
      smph->count--;
      spinlock_release(&smph->lock);
      exit_critical(cpsr);
      return E_OK;
    }

    if (timeout_ms == 0) {
      spinlock_release(&smph->lock);
      exit_critical(cpsr);
      return E_TIMED_OUT;
    }

    if ((timeout_ms > 0) && (scheduler_get_tick_count() >= deadline)) {
      spinlock_release(&smph->lock);
      exit_critical(cpsr);
      return E_TIMED_OUT;
    }

    semaphore_add_to_blocked_list(smph, cur_tcb);

    // Lock order: semaphore's own lock (already held) outer, this core's
    // scheduler lock inner — never the reverse, see scheduler_lock()'s doc.
    scheduler_lock(core_id);
    scheduler_remove_from_ready_list(&cur_tcb);
    if (timeout_ms > 0) {
      scheduler_add_to_blocked_list(cur_tcb, deadline);
    }
    scheduler_unlock(core_id);

    cur_tcb->current_state = TASK_STATE_BLOCKED;
#ifdef RTOS_TELEMETRY
    telemetry_report_task_blocked(cur_tcb->task_id, core_id, SYNC_KIND_SEMAPHORE, smph->sync_id);
#endif
    spinlock_release(&smph->lock);
    exit_critical(cpsr);

    while (cur_tcb->current_state == TASK_STATE_BLOCKED) {}

    uint32_t cpsr2 = enter_critical();
    spinlock_acquire(&smph->lock);
    if (cur_tcb->wakeup_reason == WAKEUP_REASON_RESOURCE_ACQUIRED) {
      cur_tcb->wakeup_reason = WAKEUP_REASON_NONE;
      spinlock_release(&smph->lock);
      exit_critical(cpsr2);
      return E_OK;
    }
    spinlock_release(&smph->lock);
    exit_critical(cpsr2);
  }
}

StatusCode semaphore_give(Semaphore *smph)
{
  uint32_t cpsr = enter_critical();
  spinlock_acquire(&smph->lock);

  if (smph->semaphore_blocked_list.num_items > 0) {
    TaskControlBlock *p_next_tcb = smph->semaphore_blocked_list.head->owner;
    smph->semaphore_blocked_list.head = smph->semaphore_blocked_list.head->next;
    if (smph->semaphore_blocked_list.head != NULL) {
      smph->semaphore_blocked_list.head->prev = NULL;
    }
    else {
      smph->semaphore_blocked_list.list_end = NULL;
    }
    smph->semaphore_blocked_list.num_items--;

    // The waiter may belong to a different core than the one calling
    // semaphore_give() — lock *its* core, not the caller's. Lock order:
    // semaphore's own lock (already held) outer, target core's lock inner.
    uint32_t core_id = p_next_tcb->core_id;
    scheduler_lock(core_id);
    scheduler_remove_from_blocked_list(p_next_tcb);
    p_next_tcb->event_list_item.next = NULL;
    p_next_tcb->event_list_item.prev = NULL;
    p_next_tcb->event_list_item.container = NULL;
    p_next_tcb->wakeup_reason = WAKEUP_REASON_RESOURCE_ACQUIRED;
    scheduler_add_to_ready_list(&p_next_tcb);
#ifdef RTOS_TELEMETRY
    telemetry_report_task_unblocked(p_next_tcb->task_id, core_id);
#endif
    scheduler_unlock(core_id);
  }
  else {
    if (smph->count < smph->max_count) {
      smph->count++;
    }
    else {
      spinlock_release(&smph->lock);
      exit_critical(cpsr);
      return E_RESOURCE_EXHAUSTED;
    }
  }

  spinlock_release(&smph->lock);
  exit_critical(cpsr);

  return E_OK;
}