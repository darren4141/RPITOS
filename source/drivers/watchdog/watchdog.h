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

// What the bootloader should do when wdt_reset_count exceeds wdt_reset_tolerance.
typedef enum {
  WATCHDOG_RESET_POLICY_FORCE_UPDATE = 0,  // force into DFU receive loop
  WATCHDOG_RESET_POLICY_JUMP_SLOT_B  = 1,  // reserved — app slot B (not implemented)
  WATCHDOG_RESET_POLICY_ROLLBACK     = 2,  // reserved — rollback to previous slot (not implemented)
} WatchdogResetPolicy;

// Why the watchdog reset policy was triggered. Stored in boot_flags for
// future diagnostics. Not yet written by the bootloader.
typedef enum {
  WATCHDOG_RESET_REASON_NONE           = 0,
  WATCHDOG_RESET_REASON_COUNT_EXCEEDED = 1,
} WatchdogResetReason;

// ── WDT persistent metadata (lives in eMMC EMMC_SECTOR_METADATA) ─────────────

#define WDT_META_MAGIC 0xB007DA7AU

typedef struct {
  uint32_t magic;
  uint32_t wdt_reset_count;
  int32_t  wdt_reset_tolerance;
  uint32_t wdt_reset_policy;
  uint32_t wdt_reset_reason;
} WdtMeta;

// In-RAM shadow. Call wdt_meta_read() to populate, wdt_meta_write() to persist.
extern WdtMeta wdt_meta;

StatusCode wdt_meta_read(void);
StatusCode wdt_meta_write(void);

// ─────────────────────────────────────────────────────────────────────────────

// Returns true if the previous boot was caused by a watchdog timeout.
// Call before watchdog_init() — writing PM_RSTC on init may clear the
// sticky bits in PM_RSTS.
bool watchdog_was_wdt_reset(void);

// Arm the watchdog with a [1, 15] second timeout (clamped). Sets the reset
// policy and tolerance: after tolerance watchdog resets the bootloader applies
// policy. A negative tolerance means the policy never fires (infinite retries).
StatusCode watchdog_init(uint32_t timeout_s, WatchdogResetPolicy policy, int32_t tolerance);

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
