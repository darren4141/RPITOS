#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "status.h"
#include "task_types.h"

typedef struct Semaphore Semaphore;

struct Semaphore {
  uint8_t max_count;
  uint8_t count;
  List semaphore_blocked_list;
};

void semaphore_init(Semaphore *smph, uint8_t max_count);
StatusCode semaphore_take(Semaphore *smph, int64_t delay_ms);
StatusCode semaphore_give(Semaphore *smph);


#endif