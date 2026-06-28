#ifndef RESET_H
#define RESET_H

// Triggers a full SoC reset via the PM watchdog. Re-runs the GPU's boot
// stage and reloads the bootloader from the SD card — self-healing against
// memory corruption, but slow and not verified to preserve .shared_flags.
// Never returns. Currently unused — kept for a future "things are really
// broken" fallback.
void system_hard_reset(void);

// Quiesces the app's peripherals (generic timer, GIC, LED) and jumps
// directly to the bootloader at BOOTLOADER_START_ADDR — no hardware reset,
// so RAM (including .shared_flags) is untouched with certainty. Faster than
// system_hard_reset() and doesn't depend on the PM watchdog trick, but
// can't recover if the bootloader's own code/data in RAM was corrupted by
// the app. Never returns.
void enter_bootloader(void);

#endif
