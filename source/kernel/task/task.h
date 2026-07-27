#ifndef TASK_H
#define TASK_H

#include "heap.h"
#include "scheduler.h"
#include "status.h"
#include "task_types.h"
#include <stdint.h>

/**
 * @brief Allocate a TCB and stack from the static pools, initialize the task's stack frame, and add it to the ready list.
 * @return E_RESOURCE_EXHAUSTED if the TCB pool is full, E_OUT_OF_MEM if the stack allocation fails.
 */
StatusCode task_create(TaskFunction task_function, uint16_t stack_depth, TaskPriorityLevel priority, void *task_params, TaskControlBlock **p_task_control_block);

#endif
