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
