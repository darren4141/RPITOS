#include "watchdog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "crc.h"
#include "emmc.h"

#ifndef WATCHDOG_MINIMAL
#include "scheduler.h"
#include "task.h"
#endif

#define PM_RSTC             (*(volatile uint32_t *)(PM_BASE + PM_RSTC_OFFSET))
#define PM_WDOG             (*(volatile uint32_t *)(PM_BASE + PM_WDOG_OFFSET))
#define PM_RSTS             (*(volatile uint32_t *)(PM_BASE + PM_RSTS_OFFSET))

#define PM_RSTC_WRCFG_MASK  0x030UL
#define PM_RSTS_HADWRQ      (1UL << 6)    // sticky: watchdog caused last reset
#define PM_WDOG_COUNT_MASK  0x000FFFFFUL
#define PM_WDOG_TICKS_PER_S 65536UL       // BCM2711 PM oscillator after division
#define PM_WDOG_MAX_TIMEOUT 15UL          // floor(0xFFFFF / 65536)

static uint32_t s_timeout_ticks;

// ── WDT persistent metadata ───────────────────────────────────────────────────

WdtMeta wdt_meta = {
  .magic = 0U,
  .wdt_reset_count = 0U,
  .wdt_reset_tolerance = -1,
  .wdt_reset_policy = 0U,
  .wdt_reset_reason = 0U,
  .active_app_slot = APP_SLOT_A,
  .app_slot_trial = 0U,
  .trial_boot_count = 0U,
  .crc = 0U,
};

static uint8_t s_sector_buf[SECTOR_SIZE] __attribute__((aligned(4)));

// CRC32 over every field of the struct except the trailing crc field itself.
static uint32_t wdt_meta_compute_crc(const WdtMeta *m)
{
  CRC32_t ctx;
  crc32_start(&ctx);
  crc32_update(&ctx, (const uint8_t *)m, offsetof(WdtMeta, crc));
  return crc32_finish(&ctx);
}

static void wdt_meta_set_defaults(void)
{
  wdt_meta.magic = WDT_META_MAGIC;
  wdt_meta.wdt_reset_count = 0U;
  wdt_meta.wdt_reset_tolerance = -1;
  wdt_meta.wdt_reset_policy = 0U;
  wdt_meta.wdt_reset_reason = 0U;
  wdt_meta.active_app_slot = APP_SLOT_A;
  wdt_meta.app_slot_trial = 0U;
  wdt_meta.trial_boot_count = 0U;
  wdt_meta.crc = 0U;
}

StatusCode wdt_meta_read(void)
{
  StatusCode ret = emmc_read_blocks(EMMC_SECTOR_METADATA, s_sector_buf, 1U);
  if (ret != E_OK) {
    return ret;
  }

  const WdtMeta *src = (const WdtMeta *)s_sector_buf;

  // Both the magic and a CRC over the struct must hold — magic alone can survive
  // a torn write that corrupted the A/B slot fields.
  if ((src->magic != WDT_META_MAGIC) || (src->crc != wdt_meta_compute_crc(src))) {
    wdt_meta_set_defaults();
  }
  else {
    wdt_meta = *src;
  }

  return E_OK;
}

StatusCode wdt_meta_write(void)
{
  for (uint32_t i = 0U; i < SECTOR_SIZE; i++) {
    s_sector_buf[i] = 0U;
  }

  wdt_meta.magic = WDT_META_MAGIC;
  wdt_meta.crc = wdt_meta_compute_crc(&wdt_meta);

  WdtMeta *dst = (WdtMeta *)s_sector_buf;
  *dst = wdt_meta;

  return emmc_write_blocks(EMMC_SECTOR_METADATA, s_sector_buf, 1U);
}

StatusCode wdt_meta_confirm_slot(void)
{
  StatusCode ret = wdt_meta_read();
  if (ret != E_OK) {
    return ret;
  }

  wdt_meta.app_slot_trial = 0U;
  wdt_meta.trial_boot_count = 0U;

  return wdt_meta_write();
}

// ─────────────────────────────────────────────────────────────────────────────

bool watchdog_was_wdt_reset(void)
{
  return (PM_RSTS & PM_RSTS_HADWRQ) != 0;
}

StatusCode watchdog_init(uint32_t timeout_s, WatchdogResetPolicy policy, int32_t tolerance)
{
  if (timeout_s == 0) {
    return E_INVALID_ARGS;
  }
  if (timeout_s > PM_WDOG_MAX_TIMEOUT) {
    timeout_s = PM_WDOG_MAX_TIMEOUT;
  }

  s_timeout_ticks = (timeout_s * PM_WDOG_TICKS_PER_S) & PM_WDOG_COUNT_MASK;

  PM_WDOG = PM_PASSWORD | s_timeout_ticks;
  PM_RSTC = PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;

  emmc_init();   // idempotent — no-op if already initialized
  if (wdt_meta_read() == E_OK) {
    wdt_meta.wdt_reset_tolerance = tolerance;
    wdt_meta.wdt_reset_policy = (uint32_t)policy;
    wdt_meta_write();
  }

  return E_OK;
}

void watchdog_kick(void)
{
  PM_WDOG = PM_PASSWORD | s_timeout_ticks;
}

void watchdog_disable(void)
{
  // Read-modify-write: preserve non-WRCFG bits, zero WRCFG to stop countdown.
  // Password must be present on every write or the PM block ignores it.
  uint32_t rstc = PM_RSTC & ~PM_RSTC_WRCFG_MASK;
  PM_RSTC = PM_PASSWORD | rstc;
}

void watchdog_trigger_reset(void)
{
  PM_WDOG = PM_PASSWORD | 10U;     // ~0.15 ms at 65536 ticks/s
  PM_RSTC = PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
  while (1) {}
}

// ── Kick task (excluded in WATCHDOG_MINIMAL builds) ──────────────────────────
#ifndef WATCHDOG_MINIMAL

static int64_t s_confirm_delay_ms = 0;
static TaskControlBlock *s_watchdog_kick_tcb = NULL;
static TaskControlBlock *s_watchdog_confirm_tcb = NULL;

static void watchdog_kick_task(void *params)
{
  (void)params;
  while (1) {
    watchdog_kick();
    task_delay_ms(2000);
  }
}

static void watchdog_confirm_task(void *params)
{
  (void)params;

  task_delay_ms(s_confirm_delay_ms);
  wdt_meta_confirm_slot();

  while (1) {
    task_delay_ms(60000);
  }
}

StatusCode watchdog_set_confirm_slot_timing(uint64_t new_time_ms)
{
  s_confirm_delay_ms = new_time_ms;
}

StatusCode watchdog_task_start(void)
{
  StatusCode ret;

  ret = task_create(watchdog_kick_task, 512, TASK_PRIORITY_1, NULL, &s_watchdog_kick_tcb);
  if (ret != E_OK) {
    return ret;
  }

  if (s_confirm_delay_ms >= 0) {
    ret = task_create(watchdog_confirm_task, 512, TASK_PRIORITY_1, NULL, &s_watchdog_confirm_tcb);
    if (ret != E_OK) {
      return ret;
    }
  }

  return ret;
}

#endif
