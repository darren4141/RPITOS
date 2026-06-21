#ifndef MUTEX_H
#define MUTEX_H

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
void mutex_lock(Mutex *mtx);
void mutex_unlock(Mutex *mtx);

#endif