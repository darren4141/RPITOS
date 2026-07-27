#ifndef RESET_H
#define RESET_H

/**
 * @brief Full SoC reset via the PM watchdog; reloads the bootloader from the SD card. Never returns.
 * @note See docs.md for why this exists alongside enter_bootloader().
 */
void system_hard_reset(void);

/**
 * @brief Quiesce app peripherals and jump directly to the bootloader in RAM. Never returns.
 * @note See docs.md for why this exists alongside system_hard_reset().
 */
void enter_bootloader(void);

#endif
