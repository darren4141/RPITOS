#include "mutex.h"

#include <stddef.h>

#include "interrupts.h"
#include "scheduler.h"

void mutex_init(Mutex *mtx)
{
  mtx->mutex_owner                    = NULL;
  mtx->mutex_blocked_list.head        = NULL;
  mtx->mutex_blocked_list.index       = NULL;
  mtx->mutex_blocked_list.list_end    = NULL;
  mtx->mutex_blocked_list.num_items   = 0;
  mtx->state                          = MUTEX_STATE_UNLOCKED;
  mtx->inheritance_enabled            = 0;
  mtx->inherited_priority             = TASK_PRIORITY_IDLE;
}

// Enable or disable priority inheritance.  Only allowed when the mutex is idle
// (unlocked with no waiters) to avoid inconsistent state mid-transfer.
StatusCode mutex_set_inheritance(Mutex *mtx, uint8_t enable)
{
  uint32_t cpsr = enter_critical();
  if (mtx->state == MUTEX_STATE_LOCKED || mtx->mutex_blocked_list.num_items > 0) {
    exit_critical(cpsr);
    return E_INVALID_ARGS;
  }
  mtx->inheritance_enabled = enable;
  exit_critical(cpsr);
  return E_OK;
}

static void mutex_add_to_blocked_list(Mutex *mtx, TaskControlBlock *tcb)
{
  ListItem *ei = &tcb->event_list_item;
  ei->owner     = tcb;
  ei->container = &mtx->mutex_blocked_list;
  ei->next      = NULL;
  ei->prev      = mtx->mutex_blocked_list.list_end;

  if (mtx->mutex_blocked_list.list_end != NULL) {
    mtx->mutex_blocked_list.list_end->next = ei;
  } else {
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
    mtx->state       = MUTEX_STATE_LOCKED;
    mtx->mutex_owner = cur_tcb;
    if (mtx->inheritance_enabled) {
      cur_tcb->mutexes_held++;
    }
    exit_critical(cpsr);
    return E_OK;
  }

  if (timeout_ms == 0) {
    exit_critical(cpsr);
    return E_TIMED_OUT;
  }

  mutex_add_to_blocked_list(mtx, cur_tcb);
  scheduler_remove_from_ready_list(&cur_tcb);

  if (timeout_ms > 0) {
    scheduler_add_to_blocked_list(cur_tcb, scheduler_get_tick_count() + (uint64_t)timeout_ms);
  }

  // Priority inheritance: if we're higher priority than all current waiters,
  // update inherited_priority and boost the holder if needed.
  if (mtx->inheritance_enabled) {
    if (cur_tcb->priority > mtx->inherited_priority) {
      mtx->inherited_priority = cur_tcb->priority;
    }
    if (mtx->mutex_owner->priority < mtx->inherited_priority) {
      scheduler_change_task_priority(mtx->mutex_owner, mtx->inherited_priority);
    }
  }

  cur_tcb->current_state = TASK_STATE_BLOCKED;
  exit_critical(cpsr);

  while (cur_tcb->current_state == TASK_STATE_BLOCKED) {}

  uint32_t cpsr2 = enter_critical();
  StatusCode result = (mtx->mutex_owner == cur_tcb) ? E_OK : E_TIMED_OUT;
  exit_critical(cpsr2);

  return result;
}

void mutex_unlock(Mutex *mtx)
{
  uint32_t cpsr = enter_critical();

  if (mtx->state != MUTEX_STATE_LOCKED) {
    exit_critical(cpsr);
    return;
  }

  TaskControlBlock *holder = mtx->mutex_owner;

  if (mtx->mutex_blocked_list.num_items == 0) {
    mtx->mutex_owner        = NULL;
    mtx->state              = MUTEX_STATE_UNLOCKED;
    mtx->inherited_priority = TASK_PRIORITY_IDLE;

    if (mtx->inheritance_enabled && holder != NULL) {
      holder->mutexes_held--;
      if (holder->mutexes_held == 0 && holder->priority != holder->base_priority) {
        scheduler_change_task_priority(holder, holder->base_priority);
      }
    }
  } else {
    // O(n) scan: find the highest-priority waiter (next_owner) and simultaneously
    // compute the second-highest priority (becomes inherited_priority after transfer).
    ListItem *best       = mtx->mutex_blocked_list.head;
    ListItem *iter       = best->next;
    TaskPriorityLevel second_best = TASK_PRIORITY_IDLE;

    while (iter != NULL) {
      if (iter->owner->priority > best->owner->priority) {
        if (best->owner->priority > second_best) {
          second_best = best->owner->priority;
        }
        best = iter;
      } else {
        if (iter->owner->priority > second_best) {
          second_best = iter->owner->priority;
        }
      }
      iter = iter->next;
    }

    TaskControlBlock *next_owner = best->owner;

    // Pop 'best' from the blocked list (mid-list removal via prev/next pointers).
    if (best->prev != NULL) {
      best->prev->next = best->next;
    } else {
      mtx->mutex_blocked_list.head = best->next;
    }
    if (best->next != NULL) {
      best->next->prev = best->prev;
    } else {
      mtx->mutex_blocked_list.list_end = best->prev;
    }
    mtx->mutex_blocked_list.num_items--;
    best->next      = NULL;
    best->prev      = NULL;
    best->container = NULL;

    mtx->inherited_priority = second_best;

    // Restore holder priority (FreeRTOS approach: only demote when all
    // inheritance mutexes have been released).
    if (mtx->inheritance_enabled && holder != NULL) {
      holder->mutexes_held--;
      if (holder->mutexes_held == 0 && holder->priority != holder->base_priority) {
        scheduler_change_task_priority(holder, holder->base_priority);
      }
    }

    // Transfer ownership.
    mtx->mutex_owner = next_owner;
    if (mtx->inheritance_enabled) {
      next_owner->mutexes_held++;
      // Boost new owner if higher-priority tasks are still waiting behind it.
      if (mtx->inherited_priority > next_owner->priority) {
        scheduler_change_task_priority(next_owner, mtx->inherited_priority);
      }
    }

    scheduler_remove_from_blocked_list(next_owner);
    scheduler_add_to_ready_list(&next_owner);
  }

  exit_critical(cpsr);
}
