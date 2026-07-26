#ifndef TASK_H
#define TASK_H

#include "heap.h"
#include "scheduler.h"
#include "status.h"
#include "task_types.h"
#include <stdint.h>

StatusCode task_create(TaskFunction_t taskFunction, uint16_t stack_depth, TaskPriorityLevel priority, void *taskParams, TaskControlBlock **p_task_control_block);

// Verify every task's bottom-of-stack watermark; on breach, report the culprit
// over UART and halt. Fault-safe (uart_tx_raw only) — call from the tick.
void task_check_stacks(void);

// Intact watermark words remaining below the stack high-water line.
uint32_t task_stack_free_words(const TaskControlBlock *tcb);
#endif