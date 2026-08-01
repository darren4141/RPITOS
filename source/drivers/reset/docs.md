# reset

Two ways to get back to the bootloader, with different tradeoffs. See
`reset.h` for the one-line summary of each; the reasoning behind having
both is below.

## `system_hard_reset()`

Triggers a full SoC reset via the PM watchdog. Re-runs the GPU's boot stage
and reloads the bootloader from the SD card — self-healing against memory
corruption, but slow and not verified to preserve `.shared_flags`. Never
returns. Currently unused — kept for a future "things are really broken"
fallback.

## `enter_bootloader()`

Quiesces the app's peripherals (generic timer, GIC, LED) and jumps directly
to the bootloader at `BOOTLOADER_START_ADDR` — no hardware reset, so RAM
(including `.shared_flags`) is untouched with certainty. Faster than
`system_hard_reset()` and doesn't depend on the PM watchdog trick, but can't
recover if the bootloader's own code/data in RAM was corrupted by the app.
Never returns.

Also calls `companion_core_reset_active()` first, since this is core-0-only —
without it, any companion core released via `companion_core_start()` would
keep running through the reboot instead of being re-parked, requiring a
physical power cycle to recover. See `companion_core/docs.md`'s "Re-parking
for a DFU/software reboot".
