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

## Park stack

`_companion_core_park$` (the SGI diversion target above) needs a stack to
run on, but it must not reuse the abandoned task's own stack: that stack's
remaining headroom depends entirely on how deep the interrupted task
happened to be when the IPI landed, and the fresh bring-up chain it's about
to run (`gic_percore_init`/`gentimer_init`/`scheduler_init`/`task_create`)
needs real depth of its own. Reusing the abandoned stack produced repeated
crashes with `TASK_WATERMARK` showing up as a return address — the park
code was running far enough into the old task's watermark-filled tail that
it started popping watermark words as saved registers/PC.

The fix is `g_companion_core_park_stack[COMPANION_CORE_MAX_CORES][2048]` — a
dedicated per-core stack, sized the same as a normal task stack (2048 words)
for the same reason those are sized that way. It's a global, not `static`,
because `startup.s` needs to reach it by symbol name (same pattern as
`scheduler.c`'s `p_task_control_block`).

## Alive mask

`g_core_alive_mask` tracks which cores have been released (bit N = core N
released, not yet reset) so `companion_core_reset_active()` knows who to
SGI. Only core 0 ever calls `companion_core_start()`/
`companion_core_reset_active()` in every current sample, so a plain
`volatile` is enough — same single-writer reasoning as `uart_task_started`
in `uart.c`.

## Multicore watchdog

`CompanionCoreContext` is a second, unrelated tracking mechanism — don't
conflate it with `g_core_alive_mask` above. It exists so `watchdog.c`'s
multicore mode can gate the real PM watchdog kick on every active core
reporting alive, not just core 0.

The caller owns a `CompanionCoreContext` (static/global storage — 
`companion_core_init()` stores the pointer, not a copy) and can hand the same
pointer to `WatchdogConfig.companion_core_ctx` (see `watchdog/docs.md`) so
both drivers see the same live state.

- `expected_mask`: bit N set the moment core 0 releases core N —
  `companion_core_start()` sets it itself, as part of the release, not the
  target core reporting in on its own. Core 0's own bit is set by
  `companion_core_init()`. This is deliberate: if it were the *released*
  core setting its own bit after finishing its bring-up, a core that never
  makes it that far (crashes in early startup, stuck before its first
  instruction even runs) would simply never appear in the set and the
  watchdog's AND-gate would silently ignore it forever. Setting the bit at
  release time means a core that's commanded to start but never boots still
  counts as "expected to report" — so it never kicks, the gate never closes,
  and the hardware timeout fires. That's the actual point of the multicore
  gate: catching a core that never comes up, not just one that comes up and
  later hangs.
- `core_kicked[N]`: written only by core N (single-writer per slot, safe
  without atomics — same reasoning as `g_core_alive_mask`). `watchdog.c`
  reads across all slots and resets them once every bit in `expected_mask`
  has reported in for the round.

Each companion core calls `watchdog_core_task_start()` once, near the end of
its own bring-up (after `scheduler_init()`, before `scheduler_start()`), to
begin reporting kicks — see `watchdog/docs.md`'s "Multicore mode" section for
the aggregation logic and why any core (not just core 0) safely performs the
actual `PM_WDOG` write.
