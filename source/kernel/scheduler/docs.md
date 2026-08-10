# scheduler

Preemptive, priority-based, round-robin-within-priority scheduler. Each
priority level (`TaskPriorityLevel`) has its own ready list; the scheduler
always runs the highest non-empty priority, round-robining between tasks at
that priority on each tick.

## Weak-symbol hooks

`software_timer_tick()` and `dfu_trigger_task_start()` are declared
`__attribute__((weak))` with no-op bodies in `scheduler.c`. `scheduler_init()`
calls both unconditionally, so every app gets DFU recovery for free without
opting in, and the scheduler stays linkable without forcing every sample to
pull in `software_timer.o`/`dfu_trigger.o`. If a sample links the real
`software_timer.c`/`dfu_trigger.c`, the strong definition there overrides
the weak one automatically at link time.

## Locking model

Each core has its own `Spinlock` (`SchedulerCore.lock`) guarding that core's
own ready/blocked lists — not one global lock, so routine ticking on core N
never contends core M's. A task's `core_id` never changes (no task
migration), so every scheduler operation only ever needs exactly one core's
lock.

`scheduler_lock(core_id)`/`scheduler_unlock(core_id)` take the core id
explicitly rather than always operating on the caller's own core, because
callers fall into two groups:
- same-core callers (`scheduler_switch_context`, `timer_tick_handler`,
  `task_create`, `block_until`) pass `companion_core_id()`.
- cross-core callers (`semaphore_give`, `mutex_unlock`,
  `scheduler_change_task_priority`) pass the *target* task's `tcb->core_id`
  — this is what lets a wake-up issued on one core correctly move a task
  that belongs to a different core's scheduler.

`scheduler_add_to_ready_list()`/`scheduler_remove_from_ready_list()`/
`scheduler_add_to_blocked_list()`/`scheduler_remove_from_blocked_list()`/
`scheduler_change_task_priority()` do not take the lock themselves — the
caller must already hold `scheduler_lock(tcb->core_id)` around them.

**Lock order (never reversed):** `enter_critical()` (if same-core IRQ
preemption safety is also needed) → a `Semaphore`/`Mutex`'s own lock → the
target core's `scheduler_lock()`. Taking a `scheduler_lock()` and then a
sync object's lock, on two different cores, is the classic way to deadlock
them against each other — see `spinlock/docs.md`.

Never hold `scheduler_lock()` across a blocking wait (a spin-on-task-state
loop) — acquire/release around each short list-touching step instead.

## Stack watermark check

`scheduler_switch_context()` checks `stack_base[0] == TASK_WATERMARK` for
the *outgoing* task on every switch and hangs (spins forever) if it's been
overwritten — this is deliberate, so JTAG can inspect `task_id`/`stack_base`
at the exact point of overflow rather than continuing to run on corrupted
state.

## IRQ enable timing

Don't `cpsie i` between task creation and `scheduler_start()`.
`p_task_control_block[core_id]` (read unconditionally by `cntx_switch$` in
`startup/startup.s` on every IRQ) is only ever set inside
`scheduler_switch_context()`, itself only reachable via `scheduler_start()`.
An IRQ landing before that dereferences NULL and data-aborts. No explicit
enable is needed: `task_init_stack()` sets IRQs enabled in every task's saved
SPSR, so `start_first_task()`'s `rfeia` turns them on at the right moment on
its own. See `md/client/device/instrumentation.md` for the hardware
crash-loop this was found from.

## Telemetry hooks (`RTOS_TELEMETRY` builds only)

- Idle never goes through `task_create()`, so `scheduler_init()` sends its
  one-shot `PKT_TASK_CREATED` report directly, inline.
- `scheduler_switch_context()` reports the newly-running task/state into that
  core's telemetry tick-ring on every switch.
- `block_until()` and `timer_tick_handler()`'s timeout path report
  block/unblock transitions.

See `telemetry/docs.md` and `md/client/device/instrumentation.md` for the
full design and hook-site rationale.
