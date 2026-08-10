#include "companion_core.h"

#include "gic.h"
#include "memory_map.h"
#include "telemetry.h"

// Fixed physical address used by core 0 to signal companion cores
static volatile uintptr_t *const g_core_mailbox =
  (volatile uintptr_t *)CORE_MAILBOX_ADDR;

// Bit N set = core N released and not yet reset. Single-writer (core 0)
static volatile uint32_t g_core_alive_mask = 0;

// Dedicated per-core park stack
#define COMPANION_CORE_PARK_STACK_WORDS 2048
uint32_t g_companion_core_park_stack[COMPANION_CORE_MAX_CORES][COMPANION_CORE_PARK_STACK_WORDS]
__attribute__((aligned(8)));

uint32_t companion_core_id(void)
{
  uint32_t mpidr;
  __asm__ volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r" (mpidr));
  return mpidr & 0x3U;
}

StatusCode companion_core_start(uint32_t core_id, void (*entry)(void))
{
  if ((core_id == 0U) || (core_id >= COMPANION_CORE_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  // Publish the entry, then wake the parked core. D-cache is off on every core,
  // so the store reaches RAM directly; dsb orders it before the sev wakeup.
  g_core_mailbox[core_id] = (uintptr_t)entry;
  __asm__ volatile ("dsb sy" ::: "memory");
  __asm__ volatile ("sev");
  g_core_alive_mask |= (1U << core_id);
#ifdef RTOS_TELEMETRY
  telemetry_report_boot_milestone(BOOT_MS_CORE_RELEASED, BOOT_STAGE_APP, core_id);
#endif
  return E_OK;
}

void companion_core_reset_active(void)
{
  for (uint32_t core_id = 1U; core_id < COMPANION_CORE_MAX_CORES; core_id++) {
    if (g_core_alive_mask & (1U << core_id)) {
      gic_send_mailbox_ipi(core_id);
    }
  }
  g_core_alive_mask = 0;
}
