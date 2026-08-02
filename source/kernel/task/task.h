#ifndef TASK_H
#define TASK_H

#include "heap.h"
#include "scheduler.h"
#include "status.h"
#include "task_types.h"
#include <stdint.h>

/**
 * @brief Allocate a TCB and stack from the static pools, initialize the task's stack frame, and add it to the ready list.
 * @note name is copied into the TCB, truncated to TASK_NAME_MAX - 1 chars and NUL-terminated; NULL is treated as an empty name.
 * @return E_RESOURCE_EXHAUSTED if the TCB pool is full, E_OUT_OF_MEM if the stack allocation fails.
 */
StatusCode task_create(TaskFunction task_function, uint16_t stack_depth, TaskPriorityLevel priority, void *task_params, const char *name, TaskControlBlock **p_task_control_block);

#endif
