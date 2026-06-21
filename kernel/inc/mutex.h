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
  MutexState state;
  TaskControlBlock *mutex_owner;
  List mutex_blocked_list;
};

void mutex_init(Mutex *mtx);
StatusCode mutex_lock(Mutex *mtx, int64_t delay_ms);
void mutex_unlock(Mutex *mtx);

#endif