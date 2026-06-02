#include "scheduler.h"

#include <stddef.h>

#include "uart.h"

static TaskControlBlock *ready_list[NUM_TASK_PRIORITIES];

static TaskControlBlock *blocked_task_list = NULL;

TaskControlBlock *p_task_control_block = NULL;    // global — visible to assembly

static volatile uint32_t *s_clk_freq;
static volatile uint64_t *s_tick_count;
static uint32_t hz;

// Initialize scheduler, link clk_freq, tick_count, and initialize ready lists
StatusCode scheduler_init(volatile uint32_t *p_clk_freq, uint32_t new_hz, volatile uint64_t *p_tick_count)
{
  if ((p_clk_freq == NULL) || (p_tick_count == NULL)) {
    return E_INVALID_ARGS;
  }

  s_clk_freq = p_clk_freq;
  s_tick_count = p_tick_count;
  hz = new_hz;
  *s_tick_count = 0;

  for (int i = 0; i < NUM_TASK_PRIORITIES; i++) {
    ready_list[i] = NULL;
  }

  return E_OK;
}

// Adds a task to the back of the ready list of it's respective priority
StatusCode addToReadyList(TaskControlBlock **tcb)
{
  if ((tcb == NULL) || (*tcb == NULL) || ((*tcb)->priority >= NUM_TASK_PRIORITIES)) {
    return E_INVALID_ARGS;
  }

  TaskControlBlock **idx = &ready_list[(*tcb)->priority];

  if (*idx == NULL) {
    ready_list[(*tcb)->priority] = (*tcb);
    (*tcb)->prev = NULL;
  }
  else {
    while ((*idx)->next != NULL) {
      idx = &(*idx)->next;
    }

    (*idx)->next = (*tcb);
    (*tcb)->prev = (*idx);
  }

  (*tcb)->next = NULL;
  (*tcb)->currentState = TASK_STATE_READY;

  return E_OK;
}

// Removes a task from the ready list of it's respective priority. Caller is responsible for setting tcb->next to a new value
StatusCode removeFromReadyList(TaskControlBlock **tcb)
{
  if ((tcb == NULL) || (*tcb == NULL) || ((*tcb)->priority >= NUM_TASK_PRIORITIES)) {
    return E_INVALID_ARGS;
  }

  TaskControlBlock **idx = &ready_list[(*tcb)->priority];

  while (*idx != NULL && (*idx)->taskId != (*tcb)->taskId) {
    idx = &(*idx)->next;
  }

  if (*idx == NULL) {
    return E_INVALID_ARGS;   // Task is not in the ready list
  }

  *idx = (*tcb)->next;
  (*tcb)->next = NULL;

  return E_OK;
}

// Scheduler performs a context switch, round-robin, highest priority takes precedence
void schedulerSwitchContext(void)
{
  // Mark current task as READY
  if ((p_task_control_block != NULL) && (p_task_control_block->currentState == TASK_STATE_RUNNING)) {
    p_task_control_block->currentState = TASK_STATE_READY;
  }

  for (int i = NUM_TASK_PRIORITIES - 1; i >= 0; i--) {
    if (ready_list[i] != NULL) {
      p_task_control_block = ready_list[i];

      // Rotate the ready list if there are 2+ tasks at this priority:

      if (p_task_control_block->next != NULL) {
        TaskControlBlock *last = p_task_control_block->next;
        while (last->next != NULL) {
          last = last->next;
        }
        ready_list[i] = p_task_control_block->next;
        p_task_control_block->next = NULL;
        last->next = p_task_control_block;
      }

      p_task_control_block->currentState = TASK_STATE_RUNNING;
      return;
    }
  }
}

// Start the scheduler by performing a context switch and starting the first task
StatusCode schedulerStart(void)
{
  schedulerSwitchContext();
  if (p_task_control_block == NULL) {
    return E_EMPTY;
  }

  startFirstTask();

  return E_OK;
}

// Blocking delay, task is blocked and added to the blocked task list
void task_delay_ms(uint64_t ticks)
{
  volatile TaskControlBlock *my_tcb = p_task_control_block;
  uint64_t wakeup_time = *s_tick_count + ticks;

  removeFromReadyList(&p_task_control_block);
  p_task_control_block->next = NULL;
  p_task_control_block->wakeup_time = wakeup_time;

  TaskControlBlock **blocked_task_list_iter = &blocked_task_list;

  if ((*blocked_task_list_iter) == NULL) {
    blocked_task_list = p_task_control_block;
  }
  else {
    if ((*blocked_task_list_iter)->wakeup_time > wakeup_time) {
      p_task_control_block->next = (*blocked_task_list_iter);
      blocked_task_list = p_task_control_block;
    }
    else {
      // Loop until the next task in the list is NULL or larger than our task
      while ((*blocked_task_list_iter)->next != NULL && (*blocked_task_list_iter)->next->wakeup_time < wakeup_time) {
        blocked_task_list_iter = &(*blocked_task_list_iter)->next;
      }
      p_task_control_block->next = (*blocked_task_list_iter)->next;
      (*blocked_task_list_iter)->next = p_task_control_block;
    }
  }

  p_task_control_block->currentState = TASK_STATE_BLOCKED;

  // Spin for the rest of the tick so we don't return to the task's loop
  while (my_tcb->currentState == TASK_STATE_BLOCKED) {}
}

// Scheduler tick handler, resets timer, increments global tick_count, and checks to unblock tasks
void __attribute__((noinline)) timer_tick_handler(void)
{
  // Read current CVAL and advance by one interval
  uint32_t lo, hi;
  __asm__ volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (lo), "=r" (hi));         // CNTP_CVAL read

  uint64_t cval = ((uint64_t)hi << 32) | lo;
  cval += (*s_clk_freq / hz);                                                   // advance by one interval

  uint32_t new_lo = (uint32_t)(cval & 0xFFFFFFFF);
  uint32_t new_hi = (uint32_t)(cval >> 32);
  __asm__ volatile ("mcrr p15, 2, %0, %1, c14" : : "r" (new_lo), "r" (new_hi)); // CNTP_CVAL write

  (*s_tick_count)++;

  while ((blocked_task_list != NULL) && (*s_tick_count >= blocked_task_list->wakeup_time)) {
    uart_printf("%d task ready!\r\n", blocked_task_list->taskId);
    TaskControlBlock *new_blocked_task_list_head = blocked_task_list->next;
    addToReadyList(&blocked_task_list);
    blocked_task_list = new_blocked_task_list_head;
  }

  if ((*s_tick_count % 5000) == 0) {
    uart_print("Heartbeat\r\n");
  }
}
