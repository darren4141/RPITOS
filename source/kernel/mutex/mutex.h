#ifndef MUTEX_H
#define MUTEX_H

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
};

/**
 * @brief Initialize a mutex to the unlocked state.
 */
void mutex_init(Mutex *mtx);

/**
 * @brief Enable or disable priority inheritance for this mutex.
 */
StatusCode mutex_set_inheritance(Mutex *mtx, uint8_t enable);

/**
 * @brief Lock the mutex, blocking up to timeout_ms. Boosts the owner's priority if inheritance is enabled and the caller is higher priority.
 */
StatusCode mutex_lock(Mutex *mtx, int64_t timeout_ms);

/**
 * @brief Unlock the mutex, restoring the owner's original priority and waking the next waiter.
 */
void mutex_unlock(Mutex *mtx);

#endif
