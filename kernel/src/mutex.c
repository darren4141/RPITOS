#include "mutex.h"

#include <stddef.h>

#include "interrupts.h"
#include "scheduler.h"

void mutex_init(Mutex *mtx)
{
  mtx->mutex_owner = NULL;
  mtx->mutex_blocked_list.head = NULL;
  mtx->mutex_blocked_list.index = NULL;
  mtx->mutex_blocked_list.list_end = NULL;
  mtx->mutex_blocked_list.num_items = 0;

  mtx->state = MUTEX_STATE_UNLOCKED;
}

void mutex_lock(Mutex *mtx)
{
  uint32_t cpsr = enter_critical();
  TaskControlBlock *cur_tcb = scheduler_get_current_task();

  if (mtx->state == MUTEX_STATE_LOCKED) {
    if (mtx->mutex_blocked_list.num_items == 0) {
      mtx->mutex_blocked_list.head = &cur_tcb->event_list_item;
    }
    else {
      mtx->mutex_blocked_list.list_end->next = &cur_tcb->event_list_item;
    }
    mtx->mutex_blocked_list.num_items++;
    mtx->mutex_blocked_list.list_end = &cur_tcb->event_list_item;

    removeFromReadyList(&cur_tcb);
    cur_tcb->currentState = TASK_STATE_BLOCKED;
    exit_critical(cpsr);

    while (cur_tcb->currentState == TASK_STATE_BLOCKED) {}
  }
  else if (mtx->state == MUTEX_STATE_UNLOCKED) {
    mtx->state = MUTEX_STATE_LOCKED;
    mtx->mutex_owner = cur_tcb;
    exit_critical(cpsr);
  }
}

void mutex_unlock(Mutex *mtx)
{
  uint32_t cpsr = enter_critical();
  if (mtx->state == MUTEX_STATE_LOCKED) {
    if (mtx->mutex_blocked_list.num_items == 0) {
      mtx->mutex_owner = NULL;
      mtx->state = MUTEX_STATE_UNLOCKED;
    }
    else {
      mtx->mutex_owner = mtx->mutex_blocked_list.head->owner;
      mtx->mutex_blocked_list.head = mtx->mutex_blocked_list.head->next;
      mtx->mutex_blocked_list.num_items--;

      if (mtx->mutex_blocked_list.num_items == 0) {
        mtx->mutex_blocked_list.list_end = NULL;
      }

      addToReadyList(&mtx->mutex_owner);
    }
  }
  exit_critical(cpsr);
}