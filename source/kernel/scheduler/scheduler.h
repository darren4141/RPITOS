#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "companion_core.h"
#include "status.h"
#include "task_types.h"

// One slot per core — indexed by TaskControlBlock.core_id, not by whichever
// core happens to be calling. This is what lets semaphore_give()/mutex_unlock()
// called on one core correctly move a task that belongs to another core's
// scheduler. Referenced directly by symbol name from startup/startup.s's
// _irq_handler and start_first_task, which index it by MPIDR & 3 — keep this
// declaration in sync with those.
extern TaskControlBlock *p_task_control_block[COMPANION_CORE_MAX_CORES];

/**
 * @brief Initialize the calling core's own scheduler instance: link its clock-frequency/tick-count cells, reset its ready/blocked lists, and create its idle task.
 * @note Each core has a fully independent scheduler (own ready lists, own
 * blocked list, own tick source) — this must be called once per core, from
 * that core, before it enables IRQs and calls scheduler_start(). Only
 * core_id == 0 wires up DFU recovery (dfu_trigger_task_start()) — that's a
 * single, system-wide service, not one per core.
 */
StatusCode scheduler_init(uint32_t core_id, volatile uint32_t *p_clk_freq, uint32_t new_hz, volatile uint64_t *p_tick_count);

/**
 * @brief Acquire the given core's scheduler lock, guarding its ready/blocked lists.
 * @note enter_critical() alone only stops same-core preemption — it does
 * nothing against a second core touching the same memory. Each core has its
 * OWN lock (not one global lock) so routine ticking on core N never contends
 * core M's — a task's core_id never changes, so every scheduler operation
 * only ever needs exactly one core's lock: same-core callers
 * (scheduler_switch_context, timer_tick_handler, task_create, block_until)
 * pass companion_core_id(); cross-core callers (semaphore_give, mutex_unlock,
 * scheduler_change_task_priority) pass the *target* task's tcb->core_id.
 * scheduler_add_to_ready_list()/scheduler_remove_from_ready_list()/
 * scheduler_add_to_blocked_list()/scheduler_remove_from_blocked_list()/
 * scheduler_change_task_priority() do not take this lock themselves — the
 * caller must hold scheduler_lock(tcb->core_id) around them. Pair with
 * enter_critical() (acquired first) if the caller also needs same-core
 * IRQ-preemption safety. If also holding a Semaphore's/Mutex's own lock,
 * acquire that first and this second — never the reverse, or two cores can
 * deadlock against each other. Never hold across a blocking wait (a
 * spin-on-task-state loop) — acquire/release around each short
 * list-touching step instead.
 */
void scheduler_lock(uint32_t core_id);

/**
 * @brief Release the lock acquired by scheduler_lock(core_id).
 */
void scheduler_unlock(uint32_t core_id);

/**
 * @brief Return the calling core's currently running task's TCB.
 */
TaskControlBlock *scheduler_get_current_task();

/**
 * @brief Pick the calling core's first task to run and hand off to it. Never returns.
 */
StatusCode scheduler_start(void);

/**
 * @brief Add a task to the back of the ready list for its priority, on its own core (tcb->core_id).
 * @note Must be called with scheduler_lock() held.
 */
StatusCode scheduler_add_to_ready_list(TaskControlBlock **tcb);

/**
 * @brief Remove a task from the ready list of its priority, on its own core (tcb->core_id).
 * @note Must be called with scheduler_lock() held.
 */
StatusCode scheduler_remove_from_ready_list(TaskControlBlock **tcb);

/**
 * @brief Perform a round-robin, priority-based context switch on the calling core: pick the highest-priority non-empty ready list and select its next task to run.
 */
void scheduler_switch_context();

/**
 * @brief Block the calling task for the given number of ticks, on its own core's tick source.
 */
void task_delay_ms(uint64_t ticks);

/**
 * @brief Block the calling task until wake_time + ticks, for drift-free periodic delays. Warns over UART if the deadline has already passed.
 */
void task_delay_until_ms(uint64_t *wake_time, uint64_t ticks);

/**
 * @brief Return the number of scheduler ticks since the calling core's scheduler_init().
 */
uint64_t scheduler_get_tick_count(void);

/**
 * @brief Insert a task into its own core's blocked list (tcb->core_id), sorted by ascending wakeup_time.
 * @note Must be called with scheduler_lock() held.
 */
StatusCode scheduler_add_to_blocked_list(TaskControlBlock *tcb, uint64_t wakeup_time);

/**
 * @brief Remove a task from its own core's blocked list (tcb->core_id), if it's in it.
 * @note Must be called with scheduler_lock() held.
 */
StatusCode scheduler_remove_from_blocked_list(TaskControlBlock *tcb);

/**
 * @brief Load the first ready task's saved context and branch to it. Never returns.
 * @note Defined in startup/startup.s, not scheduler.c.
 */
void start_first_task(void);

/**
 * @brief Move a task to a new priority level, relocating it in its own core's ready list if it's currently running or ready.
 * @note Must be called with scheduler_lock() held.
 */
void scheduler_change_task_priority(TaskControlBlock *tcb, TaskPriorityLevel new_priority);

#endif
