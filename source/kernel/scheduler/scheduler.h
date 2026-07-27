#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "status.h"
#include "task_types.h"

extern TaskControlBlock *p_task_control_block;

/**
 * @brief Link the scheduler to the shared clock-frequency and tick-count cells, reset the ready/blocked lists, create the idle task, and wire up DFU recovery.
 */
StatusCode scheduler_init(volatile uint32_t *p_clk_freq, uint32_t new_hz, volatile uint64_t *p_tick_count);

/**
 * @brief Return the currently running task's TCB.
 */
TaskControlBlock *scheduler_get_current_task();

/**
 * @brief Pick the first task to run and hand off to it. Never returns.
 */
StatusCode scheduler_start(void);

/**
 * @brief Add a task to the back of the ready list for its priority.
 */
StatusCode scheduler_add_to_ready_list(TaskControlBlock **tcb);

/**
 * @brief Remove a task from the ready list of its priority.
 */
StatusCode scheduler_remove_from_ready_list(TaskControlBlock **tcb);

/**
 * @brief Perform a round-robin, priority-based context switch: pick the highest-priority non-empty ready list and select its next task to run.
 */
void scheduler_switch_context();

/**
 * @brief Block the calling task for the given number of ticks.
 */
void task_delay_ms(uint64_t ticks);

/**
 * @brief Block the calling task until wake_time + ticks, for drift-free periodic delays. Warns over UART if the deadline has already passed.
 */
void task_delay_until_ms(uint64_t *wake_time, uint64_t ticks);

/**
 * @brief Return the number of scheduler ticks since scheduler_init().
 */
uint64_t scheduler_get_tick_count(void);

/**
 * @brief Insert a task into the blocked list, sorted by ascending wakeup_time.
 */
StatusCode scheduler_add_to_blocked_list(TaskControlBlock *tcb, uint64_t wakeup_time);

/**
 * @brief Remove a task from the blocked list, if it's in it.
 */
StatusCode scheduler_remove_from_blocked_list(TaskControlBlock *tcb);

/**
 * @brief Load the first ready task's saved context and branch to it. Never returns.
 * @note Defined in startup/startup.s, not scheduler.c.
 */
void start_first_task(void);

/**
 * @brief Move a task to a new priority level, relocating it in the ready list if it's currently running or ready.
 * @note Must be called inside a critical section.
 */
void scheduler_change_task_priority(TaskControlBlock *tcb, TaskPriorityLevel new_priority);

#endif
