#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "status.h"
#include "task_types.h"

typedef struct Semaphore Semaphore;

#define SEMAPHORE_TAKE_BLOCKING   -1
#define SEMAPHORE_TAKE_NO_TIMEOUT 0U

struct Semaphore {
  uint32_t max_count;
  uint32_t count;
  List semaphore_blocked_list;
};

void semaphore_init(Semaphore *smph, uint32_t max_count, uint32_t initial_count);
StatusCode semaphore_take(Semaphore *smph, int64_t timeout_ms);
StatusCode semaphore_give(Semaphore *smph);

#endif