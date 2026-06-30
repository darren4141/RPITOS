#include "watchdog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_flags.h"

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

  boot_flags.wdt_reset_policy    = (uint32_t)policy;
  boot_flags.wdt_reset_tolerance = tolerance;
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

static TaskControlBlock *s_watchdog_tcb = NULL;

static void watchdog_task(void *params)
{
  (void)params;
  while (1) {
    watchdog_kick();
    task_delay_ms(2000);
  }
}

StatusCode watchdog_task_start(void)
{
  return task_create(watchdog_task, 512, TASK_PRIORITY_1, NULL, &s_watchdog_tcb);
}

#endif
