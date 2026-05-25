#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "status.h"
#include "task_types.h"

StatusCode scheduler_init();

StatusCode addToReadyList(TaskControlBlock *tcb);

#endif