#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "status.h"
#include "task_types.h"

extern TaskControlBlock *p_task_control_block;

StatusCode scheduler_init(volatile uint32_t *p_clk_freq, uint32_t new_hz, volatile uint64_t *p_tick_count);

TaskControlBlock *scheduler_get_current_task();

StatusCode schedulerStart(void);

StatusCode addToReadyList(TaskControlBlock **tcb);

StatusCode removeFromReadyList(TaskControlBlock **tcb);

void schedulerSwitchContext();

void task_delay_ms(uint64_t ticks);

void task_delay_until_ms(uint64_t *wake_time, uint64_t ticks);

uint64_t scheduler_get_tick_count(void);

StatusCode scheduler_add_to_blocked_list(TaskControlBlock *tcb, uint64_t wakeup_time);

StatusCode scheduler_remove_from_blocked_list(TaskControlBlock *tcb);

void startFirstTask(void);   // defined in startup.s

#endif