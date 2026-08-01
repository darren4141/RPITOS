# companion_core

Releases secondary cores (1..3) from their bootstrap-image parking loop and
reports the calling core's id. AMP, not SMP: `companion_core_start()` only
hands a core an `entry` function pointer — there is no shared ready list or
cross-core task migration. What that `entry` function does is up to the
caller: `smp/docs.md`'s original claim that a released core "must not call
into scheduler/task" is no longer true — a released core is free to run its
own independent `scheduler_init()`/`task_create()`/`scheduler_start()`, and
`Mutex`/`Semaphore` are explicitly designed to hand tasks off between cores
this way (see `spinlock/docs.md`). See `source/samples/rtos/multicore_blink`
for a core doing exactly that.

## Cross-image handoff

Secondary cores start out parked (spinning on `wfe`) by the bootstrap
image's startup code. `companion_core_start()` writes the entry address into
`g_core_mailbox[core_id]` — a fixed physical address (`CORE_MAILBOX_ADDR`,
see `memory_map/docs.md`), not an ordinary `.bss` symbol, because the
parking loop (in the bootstrap image) and this writer (in the app image)
are compiled and linked separately and must agree on the address without
sharing a symbol table. D-cache is off on every core, so the mailbox write
reaches RAM directly; a `dsb` orders it before the `sev` that wakes the
parked core.

## Re-parking for a DFU/software reboot

A DFU soft reset (`reset.c`'s `enter_bootloader()`) only resets core 0 — it's
a plain jump, not a hardware reset, so a released companion core just keeps
running its old entry function through the reboot. Left alone, that core
would never be sitting in the mailbox park loop again, so the next
`companion_core_start()` call after reboot would have nothing listening —
requiring a physical power cycle to actually recover it (this was diagnosed
the hard way; see `md/companion_core_soft_reset_plan.md`).

`companion_core_reset_active()` fixes this: it SGIs every core released since
the last call (tracked in a simple alive bitmask, set by
`companion_core_start()`) with `COMPANION_CORE_PARK_SGI_ID`. An SGI is the
only way to redirect a *running* core's PC without a hardware reset — one
core cannot make another jump by writing shared memory alone, since the other
core only reacts to what it fetches on its own. `startup.s`'s `_irq_handler`
special-cases that SGI ID (alongside the existing timer/context-switch
special case) and, instead of returning through the normal epilogue, diverts
straight into `_companion_core_park$` — a park loop matching the bootstrap's
`_sec_park$` shape, reusing the same `CORE_MAILBOX_ADDR` protocol so a fresh
`companion_core_start()` after the reboot can release it again.

`enter_bootloader()` calls this before any of its other teardown. It's
fire-and-forget — no wait for the target core to confirm it actually parked
before core 0 proceeds to reboot. That's safe specifically because every
structure a companion core might still be touching mid-transition
(`scheduler.c`'s per-core lists, `uart.c`'s ring buffer, task pools) lives in
`.bss`, which the *next* boot's `zero_bss$` wipes before anything reads it
again, and core 0 doesn't touch any of that shared state itself between
sending the SGI and jumping to the bootloader.
