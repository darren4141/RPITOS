#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

#include "status.h"

// A GIC-level interrupt handler. Runs in IRQ context on the IRQ-mode stack,
// after the interrupt ID has been acknowledged (GICC_IAR read) and before EOI.
// Same shape as the existing uart_rx_irq_handler / uart_dma_irq_handler, so
// they register without a wrapper.
typedef void (*IrqHandler)(void);

// Dispatch-table span: SGIs 0-15, PPIs 16-31, SPIs 32+. Covers every BCM2711
// source in use (UART = 153, DMA ch7 = 119). Costs IRQ_MAX_INTID * 4 bytes of
// .bss; raise it if you register a higher-numbered INTID.
#define IRQ_MAX_INTID 256

// Register (or, with NULL, clear) the C handler for a GIC interrupt ID. Does
// NOT touch the GIC — call gic_enable_spi() (or the PPI enable) separately to
// actually let the interrupt reach the CPU. Returns E_INVALID_ARGS if intid is
// outside the table.
StatusCode irq_register(uint32_t intid, IrqHandler handler);

// Invoke the handler registered for intid, or do nothing if none is. Called
// from the assembly IRQ vector for every non-context-switch interrupt.
void irq_dispatch(uint32_t intid);

static inline uint32_t enter_critical(void)
{
  uint32_t cpsr;
  __asm volatile ("mrs %0, cpsr" : "=r"(cpsr));
  __asm volatile ("cpsid i"      ::: "memory");
  return cpsr;
}

static inline void exit_critical(uint32_t saved_cpsr)
{
  __asm volatile ("msr cpsr_c, %0" :: "r"(saved_cpsr) : "memory");
}

#endif
