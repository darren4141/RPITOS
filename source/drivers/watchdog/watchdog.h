#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

// BCM2711 Power Management block (same register layout as all prior BCM283x;
// only the base address changed to the low-peripheral alias).
#define PM_BASE                   0xFE100000UL
#define PM_PASSWORD               0x5A000000UL
#define PM_RSTC_OFFSET            0x1cUL
#define PM_WDOG_OFFSET            0x24UL
#define PM_RSTS_OFFSET            0x28UL
#define PM_RSTC_WRCFG_FULL_RESET  0x020UL

// Returns true if the previous boot was caused by a watchdog timeout.
// Call before watchdog_init() — writing PM_RSTC on init may clear the
// sticky bits in PM_RSTS.
bool watchdog_was_wdt_reset(void);

// Arm the watchdog with a [1, 15] second timeout. Values outside that range
// are clamped. After this returns, the hardware will reset unless
// watchdog_kick() is called within timeout_s seconds.
StatusCode watchdog_init(uint32_t timeout_s);

// Reset the countdown. Safe to call from any context — a single 32-bit
// MMIO write is atomic on Cortex-A72.
void watchdog_kick(void);

// Disarm the watchdog. No reset will occur after this returns.
void watchdog_disable(void);

// Trigger an immediate reset via the watchdog. Never returns.
void watchdog_trigger_reset(void);

// Watchdog kick task — kicks every 2 s. Requires watchdog_init() with a
// timeout > 2 s. Define WATCHDOG_MINIMAL to exclude this (bootloader).
#ifndef WATCHDOG_MINIMAL
StatusCode watchdog_task_start(void);
#endif

#endif
