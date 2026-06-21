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

static void mutex_add_to_blocked_list(Mutex *mtx, TaskControlBlock *tcb)
{
  ListItem *ei = &tcb->event_list_item;
  ei->owner = tcb;
  ei->container = &mtx->mutex_blocked_list;
  ei->next = NULL;
  ei->prev = mtx->mutex_blocked_list.list_end;

  if (mtx->mutex_blocked_list.list_end != NULL) {
    mtx->mutex_blocked_list.list_end->next = ei;
  }
  else {
    mtx->mutex_blocked_list.head = ei;
  }
  mtx->mutex_blocked_list.list_end = ei;
  mtx->mutex_blocked_list.num_items++;
}

StatusCode mutex_lock(Mutex *mtx, int64_t timeout_ms)
{
  uint32_t cpsr = enter_critical();
  TaskControlBlock *cur_tcb = scheduler_get_current_task();

  if (mtx->state == MUTEX_STATE_UNLOCKED) {
    mtx->state = MUTEX_STATE_LOCKED;
    mtx->mutex_owner = cur_tcb;
    exit_critical(cpsr);
    return E_OK;
  }

  // Mutex is locked.
  if (timeout_ms == 0) {
    exit_critical(cpsr);
    return E_TIMED_OUT;
  }

  mutex_add_to_blocked_list(mtx, cur_tcb);
  removeFromReadyList(&cur_tcb);

  if (timeout_ms > 0) {
    scheduler_add_to_blocked_list(cur_tcb, scheduler_get_tick_count() + (uint64_t)timeout_ms);
  }

  cur_tcb->currentState = TASK_STATE_BLOCKED;
  exit_critical(cpsr);

  while (cur_tcb->currentState == TASK_STATE_BLOCKED) {}

  // Check wakeup reason under a critical section so neither the timer nor
  // another task can mutate mutex_owner between the read and the return.
  uint32_t cpsr2 = enter_critical();
  StatusCode result = (mtx->mutex_owner == cur_tcb) ? E_OK : E_TIMED_OUT;
  exit_critical(cpsr2);

  return result;
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
      TaskControlBlock *next_owner = mtx->mutex_blocked_list.head->owner;
      ListItem *ei = &next_owner->event_list_item;

      // Pop head from mutex blocked list
      mtx->mutex_blocked_list.head = ei->next;
      if (mtx->mutex_blocked_list.head != NULL) {
        mtx->mutex_blocked_list.head->prev = NULL;
      }
      mtx->mutex_blocked_list.num_items--;
      if (mtx->mutex_blocked_list.num_items == 0) {
        mtx->mutex_blocked_list.list_end = NULL;
      }

      ei->next = NULL;
      ei->prev = NULL;
      ei->container = NULL;

      mtx->mutex_owner = next_owner;

      // Cancel the pending timeout if the waiter was also in blocked_task_list.
      // No-op for infinite-wait tasks (timeout_ms < 0) that were never added there.
      scheduler_remove_from_blocked_list(next_owner);
      addToReadyList(&next_owner);
    }
  }
  exit_critical(cpsr);
}
