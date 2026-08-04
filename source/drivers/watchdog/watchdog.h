#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

// BCM2711 Power Management block (same register layout as all prior BCM283x;
// only the base address changed to the low-peripheral alias).
#define PM_BASE                     0xFE100000UL
#define PM_PASSWORD                 0x5A000000UL
#define PM_RSTC_OFFSET              0x1cUL
#define PM_WDOG_OFFSET              0x24UL
#define PM_RSTS_OFFSET              0x28UL
#define PM_RSTC_WRCFG_FULL_RESET    0x020UL

/**
 * @brief What the bootloader should do when wdt_reset_count exceeds wdt_reset_tolerance.
 * @note See docs.md for the reset-policy vs. reset-reason distinction.
 */
typedef enum {
  WATCHDOG_RESET_POLICY_FORCE_UPDATE = 0,  // force into DFU receive loop
  WATCHDOG_RESET_POLICY_JUMP_SLOT_B  = 1,  // reserved — app slot B (not implemented)
  WATCHDOG_RESET_POLICY_ROLLBACK     = 2,  // reserved — rollback to previous slot (not implemented)
} WatchdogResetPolicy;

/**
 * @brief Why the watchdog reset policy was triggered. Stored in boot_flags for future diagnostics; not yet written by the bootloader.
 */
typedef enum {
  WATCHDOG_RESET_REASON_NONE           = 0,
  WATCHDOG_RESET_REASON_COUNT_EXCEEDED = 1,
} WatchdogResetReason;

// ── WDT persistent metadata (lives in eMMC EMMC_SECTOR_METADATA) ─────────────

#define WDT_META_MAGIC              0xB007DA7AU
#define WDT_KICK_PERIOD             2000U

// See docs.md for the A/B trial-boot mechanism this bounds.
#define APP_SLOT_TRIAL_MAX_ATTEMPTS 3U

typedef struct {
  uint32_t magic;
  uint32_t wdt_reset_count;
  int32_t wdt_reset_tolerance;
  uint32_t wdt_reset_policy;
  uint32_t wdt_reset_reason;
  uint32_t active_app_slot;    // APP_SLOT_A / APP_SLOT_B (see emmc.h)
  uint32_t app_slot_trial;     // 1 = active slot unconfirmed (A/B trial)
  uint32_t trial_boot_count;   // bootloader re-entries while on trial
  uint32_t crc;                // CRC32 over all preceding fields; MUST be last
} WdtMeta;

// In-RAM shadow. Call wdt_meta_read() to populate, wdt_meta_write() to persist.
extern WdtMeta wdt_meta;

/**
 * @brief Read/verify (magic + CRC) metadata from eMMC into the shadow.
 * @note On a bad or torn sector the shadow is reset to safe defaults (active slot A, no trial).
 */
StatusCode wdt_meta_read(void);

/**
 * @brief Recompute the CRC and persist the shadow to eMMC.
 */
StatusCode wdt_meta_write(void);

/**
 * @brief Confirm the active app slot: clears the trial flag + counter so the bootloader will not roll back.
 * @note Self-contained (reads, clears, writes) — the application calls this once healthy. No-op-safe if the slot was not on trial.
 */
StatusCode wdt_meta_confirm_slot(void);

/**
 * @brief Return true if the previous boot was caused by a watchdog timeout.
 * @note Call before watchdog_init() — writing PM_RSTC on init may clear the sticky bits in PM_RSTS.
 */
bool watchdog_was_wdt_reset(void);

/**
 * @brief Arm the watchdog with a [1, 15] second timeout (clamped).
 * @note After `tolerance` watchdog resets the bootloader applies `policy`. A
 * negative tolerance means the policy never fires (infinite retries).
 */
StatusCode watchdog_init(uint32_t timeout_s, WatchdogResetPolicy policy, int32_t tolerance);

/**
 * @brief Reset the countdown. Safe to call from any context — a single 32-bit MMIO write is atomic on Cortex-A72.
 */
void watchdog_kick(void);

/**
 * @brief Disarm the watchdog. No reset will occur after this returns.
 */
void watchdog_disable(void);

/**
 * @brief Trigger an immediate reset via the watchdog. Never returns.
 */
void watchdog_trigger_reset(void);

// Watchdog kick — kicks every 2 s via a periodic software timer running on
// the shared timer service task (cheap: one MMIO write). The confirm-slot
// check is eMMC-heavy, so it's handed off to its own dedicated task instead
// of running on that shared stack — see watchdog.c's note on the regression
// that happens if this isolation is removed. Requires watchdog_init() with a
// timeout > 2 s. Define WATCHDOG_MINIMAL to exclude this (bootloader).
#ifndef WATCHDOG_MINIMAL

/**
 * @brief Arm the periodic watchdog-kick software timer, plus the one-shot confirm-slot timer that hands off to a dedicated confirm task.
 */
StatusCode watchdog_task_start(void);

/**
 * @brief Change how long after boot the confirm-slot check considers the app unconfirmed.
 */
StatusCode watchdog_set_confirm_slot_timing(uint64_t new_time_ms);
#endif

#endif
