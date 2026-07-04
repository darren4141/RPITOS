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

void         mutex_init(Mutex *mtx);
StatusCode   mutex_set_inheritance(Mutex *mtx, uint8_t enable);
StatusCode   mutex_lock(Mutex *mtx, int64_t timeout_ms);
void         mutex_unlock(Mutex *mtx);

#endif
