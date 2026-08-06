# watchdog

## A/B trial boot

After a DFU flips the active app slot, the new slot boots "on trial". The
application must call `wdt_meta_confirm_slot()` once it reaches a known-good
state. If it does not, each bootloader re-entry increments
`WdtMeta.trial_boot_count`; once that reaches `APP_SLOT_TRIAL_MAX_ATTEMPTS`
the bootloader rolls back to the previous (untouched, still-valid) slot.

## Reset policy vs. reset reason

`WatchdogResetPolicy` (passed to `watchdog_init()`) controls what the
bootloader does once `wdt_reset_count` exceeds `wdt_reset_tolerance` —
currently only `WATCHDOG_RESET_POLICY_FORCE_UPDATE` (force into DFU receive
loop) is implemented; the slot-B and rollback policies are reserved.

`WatchdogResetReason` is a separate field, stored in `boot_flags` for future
diagnostics, recording *why* the policy fired. Not yet written by the
bootloader.

## Kick task vs. confirm task

`watchdog_task_start()` arms two software timers, both on the shared
`software_timer_service_task` (see `software_timer/docs.md`): a periodic kick
(cheap — one MMIO write, runs inline as the timer callback) and a one-shot
confirm-slot check. The confirm check only `semaphore_give()`s to wake a
dedicated `watchdog_confirm_task` — it never does the eMMC I/O
(`wdt_meta_confirm_slot()`) itself, since that's heavy enough to overflow the
timer-service task's small shared stack. See `CLAUDE.md`'s "Known Issues"
for the hardware crash-loop this split was fixed to prevent.
