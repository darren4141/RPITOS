#include <stddef.h>

#include "interrupts.h"

// Flat INTID -> handler table. Zeroed by the startup .bss clear, so every slot
// starts NULL (unregistered). Indexed directly by GIC interrupt ID for O(1)
// dispatch from IRQ context.
static IrqHandler g_handlers[IRQ_MAX_INTID];

StatusCode irq_register(uint32_t intid, IrqHandler handler)
{
  if (intid >= IRQ_MAX_INTID) {
    return E_INVALID_ARGS;
  }

  // Guard the store: registration can run while the IRQ path reads the table.
  uint32_t saved = enter_critical();
  g_handlers[intid] = handler;
  exit_critical(saved);
  return E_OK;
}

void irq_dispatch(uint32_t intid)
{
  // Runs in IRQ context on the IRQ-mode stack. A word-aligned pointer load is
  // atomic on ARMv8, so no lock is needed here.
  if (intid < IRQ_MAX_INTID && g_handlers[intid] != NULL) {
    g_handlers[intid]();
  }
}
