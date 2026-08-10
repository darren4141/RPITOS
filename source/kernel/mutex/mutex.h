#ifndef MUTEX_H
#define MUTEX_H

#include "spinlock.h"
#include "status.h"
#include "task_types.h"

typedef struct Mutex Mutex;

typedef enum {
  MUTEX_STATE_LOCKED,
  MUTEX_STATE_UNLOCKED,
} MutexState;

struct Mutex {
  MutexState         state;
  TaskControlBlock  *mutex_owner;
  List               mutex_blocked_list;
  uint8_t            inheritance_enabled;
  TaskPriorityLevel  inherited_priority;  // highest priority among current waiters
  // A mutex isn't owned by any one core (its waiters can belong to different
  // cores' schedulers), so it carries its own lock rather than relying on
  // any core's scheduler lock. See spinlock/docs.md.
  Spinlock           lock;
#ifdef RTOS_TELEMETRY
  // Assigned once by mutex_init() (telemetry_register_sync()), never mutated
  // afterward — identifies this mutex on the wire (telemetry_report_task_blocked(),
  // telemetry_report_mutex_owner_changed()). Field only exists in telemetry builds,
  // so non-telemetry builds see zero layout change — see md/client/device/sync_view.md.
  uint16_t sync_id;
#endif
};

/**
 * @brief Initialize a mutex to the unlocked state.
 * @param name Only used (broadcast once, never stored) when built with RTOS_TELEMETRY; pass NULL or a literal freely either way.
 */
void mutex_init(Mutex *mtx, const char *name);

/**
 * @brief Enable or disable priority inheritance for this mutex.
 */
StatusCode mutex_set_inheritance(Mutex *mtx, uint8_t enable);

/**
 * @brief Lock the mutex, blocking up to timeout_ms. Boosts the owner's priority if inheritance is enabled and the caller is higher priority.
 * @note Safe to call from a task on any core, including when the current
 * owner belongs to a different core than the caller.
 */
StatusCode mutex_lock(Mutex *mtx, int64_t timeout_ms);

/**
 * @brief Unlock the mutex, restoring the owner's original priority and waking the next waiter.
 * @note Safe to call even when the waiter being woken belongs to a
 * different core than the caller — it's placed back on its own core's
 * ready list.
 */
void mutex_unlock(Mutex *mtx);

#endif
