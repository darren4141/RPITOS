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
