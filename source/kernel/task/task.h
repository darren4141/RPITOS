#ifndef TASK_H
#define TASK_H

#include "heap.h"
#include "scheduler.h"
#include "status.h"
#include "task_types.h"
#include <stdint.h>

StatusCode task_create(TaskFunction_t taskFunction, uint16_t stack_depth, TaskPriorityLevel priority, void *taskParams, TaskControlBlock **p_task_control_block);

#endif