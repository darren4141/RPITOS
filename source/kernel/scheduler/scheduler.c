#include "scheduler.h"

#include <stddef.h>

#include "dfu_trigger.h"
#include "interrupts.h"
#include "smp.h"
#include "spinlock.h"
#include "uart.h"

#define IDLE_STACK_DEPTH 64

// Per-core scheduler state, including its own lock. Indexed by
// TaskControlBlock.core_id (not the calling core) wherever a TCB is
// involved, and by smp_core_id() for "operate on the calling core's own
// scheduler" entry points (scheduler_switch_context, timer_tick_handler).
// A task's core_id never changes (no task migration between cores), so every
// scheduler operation only ever needs exactly one core's lock — routine
// ticking on core N never contends core M's lock.
typedef struct {
  List ready_list[NUM_TASK_PRIORITIES];
  List blocked_task_list;
  volatile uint32_t *s_clk_freq;
  volatile uint64_t *s_tick_count;
  uint32_t hz;
  Spinlock lock;
  TaskControlBlock idle_tcb;
  StackType_t idle_stack[IDLE_STACK_DEPTH];
} SchedulerCore;

static SchedulerCore g_cores[SMP_MAX_CORES];

TaskControlBlock *p_task_control_block[SMP_MAX_CORES];    // global — visible to assembly

// Software-timer tick hook. software_timer.c provides the strong definition;
// samples that do not link the software-timer module fall back to this weak
// no-op, so the shared scheduler stays resolvable without forcing every sample
// to pull in software_timer.o. Core-0-only service (see scheduler_init()).
void software_timer_tick(uint64_t now_tick);
__attribute__((weak)) void software_timer_tick(uint64_t now_tick)
{
  (void)now_tick;
}

// DFU reboot-task hook, called unconditionally from scheduler_init() below so
// every app gets DFU recovery without opting in. dfu_trigger.c provides the
// strong definition (creates the semaphore-blocked task that safely calls
// enter_bootloader() from task context, never from the ISR that detects the
// key); samples that do not link dfu_trigger.o fall back to this weak no-op.
__attribute__((weak)) StatusCode dfu_trigger_task_start(void)
{
  return E_OK;
}

static void idle_task_func(void *params)
{
  (void)params;
  while (1) {}
}

void scheduler_lock(uint32_t core_id)
{
  spinlock_acquire(&g_cores[core_id].lock);
}

void scheduler_unlock(uint32_t core_id)
{
  spinlock_release(&g_cores[core_id].lock);
}

// Initialize the calling core's own scheduler, link clk_freq/tick_count, and initialize its ready lists
StatusCode scheduler_init(uint32_t core_id, volatile uint32_t *p_clk_freq, uint32_t new_hz, volatile uint64_t *p_tick_count)
{
  if ((p_clk_freq == NULL) || (p_tick_count == NULL) || (core_id >= SMP_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  SchedulerCore *core = &g_cores[core_id];

  spinlock_init(&core->lock);
  core->s_clk_freq = p_clk_freq;
  core->s_tick_count = p_tick_count;
  core->hz = new_hz;
  *core->s_tick_count = 0;

  for (int i = 0; i < NUM_TASK_PRIORITIES; i++) {
    core->ready_list[i].head = NULL;
    core->ready_list[i].index = NULL;
    core->ready_list[i].list_end = NULL;
    core->ready_list[i].num_items = 0;
  }

  core->blocked_task_list.head = NULL;
  core->blocked_task_list.index = NULL;
  core->blocked_task_list.list_end = NULL;
  core->blocked_task_list.num_items = 0;

  // Fill idle stack with watermark so the overflow check works for the idle task too
  for (int i = 0; i < IDLE_STACK_DEPTH; i++) {
    core->idle_stack[i] = TASK_WATERMARK;
  }

  // Initialize idle task stack — mirrors task_init_stack() in task.c
  StackType_t *top = &core->idle_stack[IDLE_STACK_DEPTH - 1];
  *top-- = 0x00000013U;                     // SPSR: SVC mode, IRQs enabled
  *top-- = (StackType_t)idle_task_func;     // PC
  *top-- = 0U;                              // LR — frame is [r0-r12][lr][pc][spsr]; idle never returns
  for (int i = 12; i >= 1; i--) {
    *top-- = 0U;                            // R12–R1
  }
  *top = 0U;                                // R0 (params)

  core->idle_tcb.stack_base = core->idle_stack;
  core->idle_tcb.stack_high = &core->idle_stack[IDLE_STACK_DEPTH - 1];
  core->idle_tcb.current_sp = top;
  core->idle_tcb.stack_depth = IDLE_STACK_DEPTH;
  core->idle_tcb.task_id = 0xFFFFU;
  core->idle_tcb.core_id = core_id;
  core->idle_tcb.priority = TASK_PRIORITY_IDLE;
  core->idle_tcb.base_priority = TASK_PRIORITY_IDLE;
  core->idle_tcb.mutexes_held = 0;
  core->idle_tcb.current_state = TASK_STATE_READY;
  core->idle_tcb.wakeup_time = 0U;
  core->idle_tcb.wakeup_reason = WAKEUP_REASON_NONE;
  core->idle_tcb.state_list_item = (ListItem) { NULL, NULL, &core->idle_tcb, NULL };
  core->idle_tcb.event_list_item = (ListItem) { NULL, NULL, &core->idle_tcb, NULL };

  TaskControlBlock *p_idle = &core->idle_tcb;
  scheduler_lock(core_id);
  scheduler_add_to_ready_list(&p_idle);
  scheduler_unlock(core_id);

  // DFU recovery is a single, system-wide service — only core 0 wires it up.
  // Apps must NOT call this themselves (it would create a second, orphaned
  // reboot task); see dfu_trigger.h.
  if (core_id == 0) {
    StatusCode dfu_ret = dfu_trigger_task_start();
    if (dfu_ret != E_OK) {
      return dfu_ret;
    }
  }

  return E_OK;
}

TaskControlBlock *scheduler_get_current_task()
{
  return p_task_control_block[smp_core_id()];
}

// Adds a task to the back of the ready list of its respective priority, on
// its own core. Caller must hold scheduler_lock((*tcb)->core_id).
StatusCode scheduler_add_to_ready_list(TaskControlBlock **tcb)
{
  if ((tcb == NULL) || (*tcb == NULL) || ((*tcb)->priority >= NUM_TASK_PRIORITIES) || ((*tcb)->core_id >= SMP_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  List *list = &g_cores[(*tcb)->core_id].ready_list[(*tcb)->priority];
  ListItem *item = &(*tcb)->state_list_item;

  item->owner = *tcb;
  item->container = list;
  item->next = NULL;
  item->prev = list->list_end;

  if (list->list_end != NULL) {
    list->list_end->next = item;
  }
  else {
    list->head = item;       // first item — becomes head and current runner
    list->index = item;
  }

  list->list_end = item;
  list->num_items++;

  (*tcb)->current_state = TASK_STATE_READY;

  return E_OK;
}

// Removes a task from the ready list of its respective priority, on its own
// core. Caller must hold scheduler_lock((*tcb)->core_id).
StatusCode scheduler_remove_from_ready_list(TaskControlBlock **tcb)
{
  if ((tcb == NULL) || (*tcb == NULL) || ((*tcb)->priority >= NUM_TASK_PRIORITIES) || ((*tcb)->core_id >= SMP_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  ListItem *item = &(*tcb)->state_list_item;
  List *list = item->container;

  if ((list == NULL) || (list != &g_cores[(*tcb)->core_id].ready_list[(*tcb)->priority])) {
    return E_INVALID_ARGS;   // not in the ready list
  }

  // Stitch the linked list
  if (item->prev != NULL) {
    item->prev->next = item->next;
  }
  else {
    list->head = item->next;      // item was the head
  }

  if (item->next != NULL) {
    item->next->prev = item->prev;
  }
  else {
    list->list_end = item->prev;   // item was the tail
  }

  // Advance index if the running task is the one being removed
  if (list->index == item) {
    list->index = (item->next != NULL) ? item->next : list->head;
  }

  list->num_items--;

  item->next = NULL;
  item->prev = NULL;
  item->container = NULL;

  return E_OK;
}

// Scheduler performs a context switch on the calling core, round-robin, highest priority takes precedence
void scheduler_switch_context(void)
{
  uint32_t core_id = smp_core_id();
  SchedulerCore *core = &g_cores[core_id];
  TaskControlBlock *current = p_task_control_block[core_id];

  // Stack watermark check: the lowest word of every task stack is initialized to
  // TASK_WATERMARK and never used for real data. If it has been overwritten the
  // stack has overflowed. Hang here so JTAG can identify the task (task_id, stack_base).
  if ((current != NULL) && (current->stack_base != NULL)
      && (current->stack_base[0] != TASK_WATERMARK)) {
    for ( ; ; ) {
    }
  }

  scheduler_lock(core_id);

  // If the current task used its quantum (not blocked), mark READY and advance
  // the round-robin index so the next task at the same priority runs next tick.
  if ((current != NULL) && (current->current_state == TASK_STATE_RUNNING)) {
    if (current->priority < NUM_TASK_PRIORITIES) {
      current->current_state = TASK_STATE_READY;
      List *list = &core->ready_list[current->priority];
      if (list->index != NULL) {
        list->index = (list->index->next != NULL) ? list->index->next : list->head;
      }
    }
  }

  // Pick the highest-priority non-empty list and run its index task
  for (int i = NUM_TASK_PRIORITIES - 1; i >= 0; i--) {
    if (core->ready_list[i].num_items > 0) {
      current = core->ready_list[i].index->owner;
      current->current_state = TASK_STATE_RUNNING;
      p_task_control_block[core_id] = current;
      scheduler_unlock(core_id);
      return;
    }
  }

  p_task_control_block[core_id] = NULL;
  scheduler_unlock(core_id);
}

// Start the calling core's scheduler by performing a context switch and starting its first task
StatusCode scheduler_start(void)
{
  uint32_t core_id = smp_core_id();

  scheduler_switch_context();
  if (p_task_control_block[core_id] == NULL) {
    return E_EMPTY;
  }

  start_first_task();

  return E_OK;
}

uint64_t scheduler_get_tick_count(void)
{
  return *g_cores[smp_core_id()].s_tick_count;
}

// Caller must hold scheduler_lock(tcb->core_id).
StatusCode scheduler_add_to_blocked_list(TaskControlBlock *tcb, uint64_t wakeup_time)
{
  if ((tcb == NULL) || (tcb->core_id >= SMP_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  List *blocked_task_list = &g_cores[tcb->core_id].blocked_task_list;

  tcb->wakeup_time = wakeup_time;

  ListItem *item = &tcb->state_list_item;
  item->owner = tcb;
  item->container = blocked_task_list;
  item->next = NULL;
  item->prev = NULL;

  if (blocked_task_list->head == NULL) {
    blocked_task_list->head = item;
    blocked_task_list->list_end = item;
  }
  else if (wakeup_time <= blocked_task_list->head->owner->wakeup_time) {
    item->next = blocked_task_list->head;
    blocked_task_list->head->prev = item;
    blocked_task_list->head = item;
  }
  else {
    ListItem *iter = blocked_task_list->head;
    while (iter->next != NULL && iter->next->owner->wakeup_time <= wakeup_time) {
      iter = iter->next;
    }
    item->next = iter->next;
    item->prev = iter;
    if (iter->next != NULL) {
      iter->next->prev = item;
    }
    else {
      blocked_task_list->list_end = item;
    }
    iter->next = item;
  }

  blocked_task_list->num_items++;
  return E_OK;
}

// Caller must hold scheduler_lock(tcb->core_id).
StatusCode scheduler_remove_from_blocked_list(TaskControlBlock *tcb)
{
  if ((tcb == NULL) || (tcb->core_id >= SMP_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  List *blocked_task_list = &g_cores[tcb->core_id].blocked_task_list;
  ListItem *item = &tcb->state_list_item;

  if (item->container != blocked_task_list) {
    return E_OK;
  }

  if (item->prev != NULL) {
    item->prev->next = item->next;
  }
  else {
    blocked_task_list->head = item->next;
  }

  if (item->next != NULL) {
    item->next->prev = item->prev;
  }
  else {
    blocked_task_list->list_end = item->prev;
  }

  blocked_task_list->num_items--;
  item->next = NULL;
  item->prev = NULL;
  item->container = NULL;

  return E_OK;
}

// Moves tcb to a new priority level.  If the task is in a ready list (READY or
// RUNNING state) it is relocated to the correct priority bucket.  If blocked,
// only the priority field is updated; scheduler_add_to_ready_list will use the new value
// when the task is eventually unblocked.  Caller must hold scheduler_lock(tcb->core_id).
void scheduler_change_task_priority(TaskControlBlock *tcb, TaskPriorityLevel new_priority)
{
  TaskState saved_state = tcb->current_state;
  int in_ready = ((saved_state == TASK_STATE_READY) || (saved_state == TASK_STATE_RUNNING));

  if (in_ready) {
    ListItem *item = &tcb->state_list_item;
    List *list = item->container;
    if (list != NULL) {
      if (item->prev != NULL) {
        item->prev->next = item->next;
      }
      else {
        list->head = item->next;
      }
      if (item->next != NULL) {
        item->next->prev = item->prev;
      }
      else {
        list->list_end = item->prev;
      }
      if (list->index == item) {
        list->index = (item->next != NULL) ? item->next : list->head;
      }
      list->num_items--;
      item->next = NULL;
      item->prev = NULL;
      item->container = NULL;
    }
  }

  tcb->priority = new_priority;

  if (in_ready) {
    scheduler_add_to_ready_list(&tcb);
    tcb->current_state = saved_state;   // restore RUNNING if it was running when boosted
  }
}

static void block_until(uint64_t wakeup_time)
{
  uint32_t core_id = smp_core_id();
  volatile TaskControlBlock *my_tcb = p_task_control_block[core_id];

  uint32_t cpsr = enter_critical();
  scheduler_lock(core_id);
  scheduler_remove_from_ready_list(&p_task_control_block[core_id]);
  scheduler_add_to_blocked_list(p_task_control_block[core_id], wakeup_time);
  p_task_control_block[core_id]->current_state = TASK_STATE_BLOCKED;
  scheduler_unlock(core_id);
  exit_critical(cpsr);

  while (my_tcb->current_state == TASK_STATE_BLOCKED) {}
}

void task_delay_ms(uint64_t ticks)
{
  block_until(scheduler_get_tick_count() + ticks);
}

void task_delay_until_ms(uint64_t *last_wake_time, uint64_t period)
{
  uint64_t next_wake = *last_wake_time + period;
  uint64_t now = scheduler_get_tick_count();
  if (now > next_wake) {
    uart_printf("WARN: task overrun by %u ticks (period %u)\r\n",
                (uint32_t)(now - next_wake), (uint32_t)period);
  }
  block_until(next_wake);
  *last_wake_time += period;
}

// Scheduler tick handler for the calling core: resets its timer, increments
// its tick_count, and checks to unblock its own tasks.
void __attribute__((noinline)) timer_tick_handler(void)
{
  uint32_t core_id = smp_core_id();
  SchedulerCore *core = &g_cores[core_id];

  // Rearm relative to the current physical counter (CNTPCT) in case any systicks got skipped
  uint32_t lo, hi;
  __asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));         // CNTPCT read

  uint64_t cntpct = ((uint64_t)hi << 32) | lo;
  uint64_t cval = cntpct + (*core->s_clk_freq / core->hz);

  uint32_t new_lo = (uint32_t)(cval & 0xFFFFFFFF);
  uint32_t new_hi = (uint32_t)(cval >> 32);
  __asm__ volatile ("mcrr p15, 2, %0, %1, c14" : : "r" (new_lo), "r" (new_hi));   // CNTP_CVAL write

  (*core->s_tick_count)++;

  scheduler_lock(core_id);
  while (core->blocked_task_list.head != NULL && *core->s_tick_count >= core->blocked_task_list.head->owner->wakeup_time) {
    TaskControlBlock *tcb = core->blocked_task_list.head->owner;

    scheduler_remove_from_blocked_list(tcb);

    // If the task was also waiting on a mutex/semaphore, remove it from that
    // list too so the owner cannot hand it the lock after it has timed out.
    ListItem *ei = &tcb->event_list_item;
    if (ei->container != NULL) {
      List *event_list = ei->container;
      if (ei->prev != NULL) {
        ei->prev->next = ei->next;
      }
      else {
        event_list->head = ei->next;
      }
      if (ei->next != NULL) {
        ei->next->prev = ei->prev;
      }
      else {
        event_list->list_end = ei->prev;
      }
      event_list->num_items--;
      ei->next = NULL;
      ei->prev = NULL;
      ei->container = NULL;
    }

    scheduler_add_to_ready_list(&tcb);
  }
  scheduler_unlock(core_id);

  // Expire software timers: move any due timers to the active list and signal
  // the software-timer service task once per expiry. Core-0-only service.
  if (core_id == 0) {
    software_timer_tick(*core->s_tick_count);
  }
}
