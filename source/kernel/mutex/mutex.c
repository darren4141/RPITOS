#include "mutex.h"

#include <stddef.h>

#include "interrupts.h"
#include "scheduler.h"
#include "telemetry.h"

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

  spinlock_init(&mtx->lock);
}

// Enable or disable priority inheritance.  Only allowed when the mutex is idle
// (unlocked with no waiters) to avoid inconsistent state mid-transfer.
StatusCode mutex_set_inheritance(Mutex *mtx, uint8_t enable)
{
  uint32_t cpsr = enter_critical();
  spinlock_acquire(&mtx->lock);
  if (mtx->state == MUTEX_STATE_LOCKED || mtx->mutex_blocked_list.num_items > 0) {
    spinlock_release(&mtx->lock);
    exit_critical(cpsr);
    return E_INVALID_ARGS;
  }
  mtx->inheritance_enabled = enable;
  spinlock_release(&mtx->lock);
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
  spinlock_acquire(&mtx->lock);
  TaskControlBlock *cur_tcb = scheduler_get_current_task();

  if (mtx->state == MUTEX_STATE_UNLOCKED) {
    mtx->state       = MUTEX_STATE_LOCKED;
    mtx->mutex_owner = cur_tcb;
    if (mtx->inheritance_enabled) {
      cur_tcb->mutexes_held++;
    }
    spinlock_release(&mtx->lock);
    exit_critical(cpsr);
    return E_OK;
  }

  if (timeout_ms == 0) {
    spinlock_release(&mtx->lock);
    exit_critical(cpsr);
    return E_TIMED_OUT;
  }

  mutex_add_to_blocked_list(mtx, cur_tcb);

  // Move the waiter onto its own core's blocked list — a self-contained
  // critical section scoped to cur_tcb->core_id only.
  scheduler_lock(cur_tcb->core_id);
  scheduler_remove_from_ready_list(&cur_tcb);
  if (timeout_ms > 0) {
    scheduler_add_to_blocked_list(cur_tcb, scheduler_get_tick_count() + (uint64_t)timeout_ms);
  }
  cur_tcb->current_state = TASK_STATE_BLOCKED;
#ifdef RTOS_TELEMETRY
  telemetry_report_task_blocked(cur_tcb->task_id, cur_tcb->core_id);
#endif
  scheduler_unlock(cur_tcb->core_id);

  // Priority inheritance: if we're higher priority than all current waiters,
  // update inherited_priority and boost the holder if needed. The holder may
  // be on a different core than cur_tcb — deliberately a second, separate
  // critical section scoped to *its* core, never held alongside cur_tcb's
  // core lock, so two different core locks are never nested here (mtx->lock,
  // held for this whole function, is what keeps this consistent against a
  // concurrent mutex_unlock() on the same mutex).
  if (mtx->inheritance_enabled) {
    if (cur_tcb->priority > mtx->inherited_priority) {
      mtx->inherited_priority = cur_tcb->priority;
    }
    if (mtx->mutex_owner->priority < mtx->inherited_priority) {
      uint32_t owner_core = mtx->mutex_owner->core_id;
      scheduler_lock(owner_core);
      scheduler_change_task_priority(mtx->mutex_owner, mtx->inherited_priority);
      scheduler_unlock(owner_core);
    }
  }

  spinlock_release(&mtx->lock);
  exit_critical(cpsr);

  while (cur_tcb->current_state == TASK_STATE_BLOCKED) {}

  uint32_t cpsr2 = enter_critical();
  spinlock_acquire(&mtx->lock);
  StatusCode result = (mtx->mutex_owner == cur_tcb) ? E_OK : E_TIMED_OUT;
  spinlock_release(&mtx->lock);
  exit_critical(cpsr2);

  return result;
}

void mutex_unlock(Mutex *mtx)
{
  uint32_t cpsr = enter_critical();
  spinlock_acquire(&mtx->lock);

  if (mtx->state != MUTEX_STATE_LOCKED) {
    spinlock_release(&mtx->lock);
    exit_critical(cpsr);
    return;
  }

  // mutex_unlock() is only ever called by the owning task on itself, so
  // holder always belongs to the calling core.
  TaskControlBlock *holder = mtx->mutex_owner;

  if (mtx->mutex_blocked_list.num_items == 0) {
    mtx->mutex_owner        = NULL;
    mtx->state              = MUTEX_STATE_UNLOCKED;
    mtx->inherited_priority = TASK_PRIORITY_IDLE;

    if (mtx->inheritance_enabled && holder != NULL) {
      holder->mutexes_held--;
      if (holder->mutexes_held == 0 && holder->priority != holder->base_priority) {
        scheduler_lock(holder->core_id);
        scheduler_change_task_priority(holder, holder->base_priority);
        scheduler_unlock(holder->core_id);
      }
    }
  } else {
    // O(n) scan: find the highest-priority waiter (next_owner) and simultaneously
    // compute the second-highest priority (becomes inherited_priority after transfer).
    // mtx's own blocked list is protected by mtx->lock (held) — no scheduler
    // lock needed for the scan itself.
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

    TaskControlBlock *next_owner = best->owner;   // may be on a different core than holder

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
    // inheritance mutexes have been released). Holder's own core — separate
    // critical section from next_owner's below (may be a different core).
    if (mtx->inheritance_enabled && holder != NULL) {
      holder->mutexes_held--;
      if (holder->mutexes_held == 0 && holder->priority != holder->base_priority) {
        scheduler_lock(holder->core_id);
        scheduler_change_task_priority(holder, holder->base_priority);
        scheduler_unlock(holder->core_id);
      }
    }

    // Transfer ownership. next_owner's own core — boost (if needed) and the
    // blocked->ready move happen together under one lock/unlock pair.
    mtx->mutex_owner = next_owner;
    scheduler_lock(next_owner->core_id);
    if (mtx->inheritance_enabled) {
      next_owner->mutexes_held++;
      // Boost new owner if higher-priority tasks are still waiting behind it.
      if (mtx->inherited_priority > next_owner->priority) {
        scheduler_change_task_priority(next_owner, mtx->inherited_priority);
      }
    }
    scheduler_remove_from_blocked_list(next_owner);
    scheduler_add_to_ready_list(&next_owner);
#ifdef RTOS_TELEMETRY
    telemetry_report_task_unblocked(next_owner->task_id, next_owner->core_id);
#endif
    scheduler_unlock(next_owner->core_id);
  }

  spinlock_release(&mtx->lock);
  exit_critical(cpsr);
}
