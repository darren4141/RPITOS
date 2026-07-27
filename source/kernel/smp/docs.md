# smp

AMP (asymmetric multiprocessing), not SMP scheduling — each core runs its
own independent `entry` function forever; there is no shared ready list or
cross-core task migration. `scheduler`/`task` are single-core; a core
started via `smp_start_core()` must not call into them.

## Cross-image handoff

Secondary cores start out parked (spinning on `wfe`) by the bootstrap
image's startup code. `smp_start_core()` writes the entry address into
`g_core_mailbox[core_id]` — a fixed physical address (`CORE_MAILBOX_ADDR`,
see `memory_map/docs.md`), not an ordinary `.bss` symbol, because the
parking loop (in the bootstrap image) and this writer (in the app image)
are compiled and linked separately and must agree on the address without
sharing a symbol table. D-cache is off on every core, so the mailbox write
reaches RAM directly; a `dsb` orders it before the `sev` that wakes the
parked core.
