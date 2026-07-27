#include "smp.h"

#include "memory_map.h"

// Cross-image release mailbox. Each secondary's bootstrap parking loop watches
// g_core_mailbox[core_id]; a non-zero value is the address it blx's to. It lives
// at a fixed physical address (shared with the bootstrap asm) rather than an
// ordinary .bss symbol, because the parking loop and this writer are in
// different images and it must survive the bootstrap -> bootloader -> app chain.
static volatile uintptr_t *const g_core_mailbox =
  (volatile uintptr_t *)CORE_MAILBOX_ADDR;

uint32_t smp_core_id(void)
{
  uint32_t mpidr;
  __asm__ volatile ("mrc p15, 0, %0, c0, c0, 5" : "=r" (mpidr));
  return mpidr & 0x3U;
}

StatusCode smp_start_core(uint32_t core_id, void (*entry)(void))
{
  if ((core_id == 0U) || (core_id >= SMP_MAX_CORES)) {
    return E_INVALID_ARGS;
  }

  // Publish the entry, then wake the parked core. D-cache is off on every core,
  // so the store reaches RAM directly; dsb orders it before the sev wakeup.
  g_core_mailbox[core_id] = (uintptr_t)entry;
  __asm__ volatile ("dsb sy" ::: "memory");
  __asm__ volatile ("sev");
  return E_OK;
}
