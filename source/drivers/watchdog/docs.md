# watchdog

## A/B trial boot

After a DFU flips the active app slot, the new slot boots "on trial". The
application must call `wdt_meta_confirm_slot()` once it reaches a known-good
state. If it does not, each bootloader re-entry increments
`WdtMeta.trial_boot_count`; once that reaches `APP_SLOT_TRIAL_MAX_ATTEMPTS`
the bootloader rolls back to the previous (untouched, still-valid) slot.

`wdt_meta_confirm_slot()` also clears `wdt_reset_count` — confirming the slot
is the "this boot is healthy" signal, so it resets the separate reset-
tolerance counter below too, not just the trial fields. Without this a
healthy, already-confirmed app would still accumulate toward
`wdt_reset_tolerance` across ordinary reboots and eventually force DFU on its
own.

## Reset policy vs. reset reason

`WatchdogResetPolicy` (passed to `watchdog_init()`) controls what the
bootloader does once `wdt_reset_count` exceeds `wdt_reset_tolerance` —
currently only `WATCHDOG_RESET_POLICY_FORCE_UPDATE` (force into DFU receive
loop) is implemented; the slot-B and rollback policies are reserved.

`wdt_reset_count` is bumped by the bootloader on *every* boot, WDT-caused or
not — it is not reset by anything except the tolerance policy firing, a fresh
DFU flash, or `wdt_meta_confirm_slot()`. `tolerance` is a small integer
threshold (`wdt_reset_count > tolerance` trips the policy), not a boolean:
0 is the strictest setting (trips on the very first WDT reset), not "never" —
use `WATCHDOG_RESET_TOLERANCE_INFINITE` (negative) to disable the policy.

`WatchdogResetReason` is a separate field, stored in `boot_flags` for future
diagnostics, recording *why* the policy fired. Not yet written by the
bootloader.

## Kick task vs. confirm task

`watchdog_task_start()` arms two software timers, both on the shared
`software_timer_service_task` (see `software_timer/docs.md`): a periodic kick
(cheap — one MMIO write, runs inline as the timer callback) and, if
`watchdog_init()` was given a non-negative `confirm_delay_ms`, a one-shot
confirm-slot check. The confirm check only `semaphore_give()`s to wake a
dedicated `watchdog_confirm_task` — it never does the eMMC I/O
(`wdt_meta_confirm_slot()`) itself, since that's heavy enough to overflow the
timer-service task's small shared stack. See `CLAUDE.md`'s "Known Issues"
for the hardware crash-loop this split was fixed to prevent.

## Confirm timing is required, not defaulted

`watchdog_init()`'s `confirm_delay_ms` has no implicit default — every caller
picks one explicitly. The choice matters: confirming too early defeats the
A/B trial mechanism (a build that's about to crash 5 s after boot can get
marked "healthy" before it does, so rollback never triggers), and never
confirming risks a false-positive rollback for an app that's actually fine
but simply never calls `wdt_meta_confirm_slot()`. Pass
`WATCHDOG_CONFIRM_MANUAL` to skip the timer and confirm explicitly from app
code once it reaches a real known-good state instead of a fixed timeout.
