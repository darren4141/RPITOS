#include "companion_core.h"

#include "gic.h"
#include "memory_map.h"

// Cross-image release mailbox — a software convention (a fixed RAM address
// both the bootstrap image and this one agree on), NOT the ARM Local
// hardware mailboxes gic_send_mailbox_ipi() uses below. Each secondary's
// bootstrap parking loop watches g_core_mailbox[core_id]; a non-zero value
// is the address it blx's to. It lives at a fixed physical address (shared
// with the bootstrap asm) rather than an ordinary .bss symbol, because the
// parking loop and this writer are in different images and it must survive
// the bootstrap -> bootloader -> app chain.
static volatile uintptr_t *const g_core_mailbox =
  (volatile uintptr_t *)CORE_MAILBOX_ADDR;

// Bit N set = companion_core_start() has released core N and it hasn't been
// reset (companion_core_reset_active()) since. Single-writer — core 0 is
// always the one calling companion_core_start()/companion_core_reset_active()
// in every sample — so a plain volatile is enough, same reasoning as
// uart_task_started in uart.c.
static volatile uint32_t g_core_alive_mask = 0;

// Dedicated per-core stack for startup.s's _companion_core_park$ (reached via
// a mailbox 0 IPI — see companion_core_reset_active()) to switch to before
// running any fresh bring-up code. NOT the abandoned task's own stack —
// reusing that turned out to be unsafe: its remaining headroom depends
// entirely on how deep the interrupted task happened to be when the IPI
// arrived, which isn't something we control, and the fresh entry function's
// own bring-up chain (gic_percore_init/gentimer_init/scheduler_init/
// task_create) needs real depth of its own. Overflowing into the abandoned
// stack's watermark-filled tail is exactly what produced repeated
// TASK_WATERMARK-as-return-address crashes during this investigation.
// Sized the same as a normal task stack (2048 words) for the same reason
// those are sized that way. Global (not static) — visible to assembly by
// symbol name, same pattern as scheduler.c's p_task_control_block.
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
