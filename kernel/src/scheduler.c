#include "scheduler.h"

static TaskControlBlock *ready_list[NUM_TASK_PRIORITIES];

TaskControlBlock *p_task_control_block = NULL;    // global — visible to assembly

StatusCode scheduler_init()
{
  for (int i = 0; i < NUM_TASK_PRIORITIES; i++) {
    ready_list[i] = NULL;
  }

  return E_OK;
}

StatusCode addToReadyList(TaskControlBlock *tcb)
{
  if ((tcb == NULL) || (tcb->priority >= NUM_TASK_PRIORITIES)) {
    return E_INVALID_ARGS;
  }

  TaskControlBlock *idx = ready_list[tcb->priority];

  if (idx == NULL) {
    ready_list[tcb->priority] = tcb;
    tcb->prev = NULL;
  }
  else {
    while (idx->next != NULL) {
      idx = idx->next;
    }

    idx->next = tcb;
    tcb->prev = idx;
  }

  return E_OK;
}

void schedulerSwitchContext(void)
{
  for (int i = NUM_TASK_PRIORITIES - 1; i >= 0; i--) {
    if (ready_list[i] != NULL) {
      p_task_control_block = ready_list[i];
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