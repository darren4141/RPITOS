#include "scheduler.h"

#include <stddef.h>

#include "uart.h"

static TaskControlBlock *ready_list[NUM_TASK_PRIORITIES];

static TaskControlBlock *blocked_task_list = NULL;

TaskControlBlock *p_task_control_block = NULL;    // global — visible to assembly

StatusCode scheduler_init()
{
  for (int i = 0; i < NUM_TASK_PRIORITIES; i++) {
    ready_list[i] = NULL;
  }

  return E_OK;
}

StatusCode addToReadyList(TaskControlBlock **tcb)
{
  if ((tcb == NULL) || (*tcb == NULL) || ((*tcb)->priority >= NUM_TASK_PRIORITIES)) {
    return E_INVALID_ARGS;
  }

  TaskControlBlock *idx = ready_list[(*tcb)->priority];

  if (idx == NULL) {
    ready_list[(*tcb)->priority] = (*tcb);
    (*tcb)->prev = NULL;
  }
  else {
    while (idx->next != NULL) {
      idx = idx->next;
    }

    idx->next = (*tcb);
    (*tcb)->prev = idx;
  }

  return E_OK;
}

void schedulerSwitchContext(void)
{
  // Mark current task as READY
  if (p_task_control_block != NULL) {
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

StatusCode schedulerStart(void)
{
  schedulerSwitchContext();
  if (p_task_control_block == NULL) {
    return E_EMPTY;
  }

  startFirstTask();

  return E_OK;
}

// void task_delay(uint64_t ticks)
// {
// uint64_t wakeup_time = read_cntpct() + ticks;

// TaskControlBlock *blocked_task_list_iter = blocked_task_list;

// if (blocked_task_list_iter == NULL) {
// blocked_task_list = p_task_control_block;
// }
// else {
// while (blocked_task_list->wakeup_time < wakeup_time) {
// }
// }
// }