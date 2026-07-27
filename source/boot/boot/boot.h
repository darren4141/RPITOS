#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

#include "status.h"

/**
 * @brief Initialize the boot component's internal sector counters.
 */
StatusCode boot_init();

/**
 * @brief Validate the app image in the given slot by recomputing the CRC over its firmware read back from eMMC.
 */
StatusCode boot_validate_app(uint32_t app_sector);

/**
 * @brief Load the app image from the given slot into RAM at APP_START_ADDR.
 */
StatusCode boot_load_app(uint32_t app_sector);

/**
 * @brief Drain the UART, disable interrupts, and jump to the loaded app. Never returns.
 */
void boot_jump_to_app();

#endif
