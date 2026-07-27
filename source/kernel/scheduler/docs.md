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
