#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

#include "status.h"

/**
 * @brief Signature for a GIC-level interrupt handler.
 * @note Runs in IRQ context on the IRQ-mode stack, after the interrupt ID has
 * been acknowledged (GICC_IAR read) and before EOI.
 */
typedef void (*IrqHandler)(void);

// Dispatch-table span: SGIs 0-15, PPIs 16-31, SPIs 32+. Raise if you register
// a higher-numbered INTID than this.
#define IRQ_MAX_INTID 256

/**
 * @brief Register (or, with NULL, clear) the C handler for a GIC interrupt ID.
 * @note Does not touch the GIC itself — call gic_enable_spi() (or the PPI
 * enable) separately to let the interrupt reach the CPU.
 */
StatusCode irq_register(uint32_t intid, IrqHandler handler);

/**
 * @brief Invoke the handler registered for intid, or do nothing if none is registered.
 * @note Called from the assembly IRQ vector for every non-context-switch interrupt.
 */
void irq_dispatch(uint32_t intid);

/**
 * @brief Disable IRQs and return the previous CPSR so the caller can restore it.
 */
static inline uint32_t enter_critical(void)
{
  uint32_t cpsr;
  __asm volatile ("mrs %0, cpsr" : "=r"(cpsr));
  __asm volatile ("cpsid i"      ::: "memory");
  return cpsr;
}

/**
 * @brief Restore a CPSR previously saved by enter_critical().
 */
static inline void exit_critical(uint32_t saved_cpsr)
{
  __asm volatile ("msr cpsr_c, %0" :: "r"(saved_cpsr) : "memory");
}

#endif
