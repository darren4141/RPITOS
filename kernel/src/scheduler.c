#include "scheduler.h"

static TaskControlBlock *ready_list[NUM_TASK_PRIORITIES];

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