#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

#include "status.h"

StatusCode boot_init();
// Validate the app image in the given slot (its header sector), by recomputing
// the CRC over its firmware read back from eMMC.
StatusCode boot_validateApp(uint32_t app_sector);
// Load the app image from the given slot into RAM at APP_START_ADDR.
StatusCode boot_loadApp(uint32_t app_sector);
void boot_jumpToApp();

#endif
