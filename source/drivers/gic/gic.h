#ifndef GIC_H
#define GIC_H

#include <stdint.h>

/**
 * @brief Initialize GIC-400 distributor-global state. Call once, from any one core (core 0), before any core calls gic_percore_init().
 * @note Disables then re-enables GICD_CTLR. Does not touch PPI enables,
 * PPI priority, the CPU interface, or timer routing — those are all
 * banked/distinct per core and live in gic_percore_init() instead.
 */
void gic_distributor_init(void);

/**
 * @brief Initialize this core's own GIC-400 CPU interface, enable its EL1 physical timer (PPI 30) IRQ, and enable its ARM Local mailbox 0 IRQ.
 * @note Must be called once by *every* core that wants to take IRQs,
 * including core 0 — GICD_ISENABLER0/GICD_IPRIORITYR (for the PPI range),
 * GICC_CTLR, GICC_PMR, CORE_TIMER_IRQCNTL, and the mailbox 0 IRQ enable are
 * all per-core state despite living at one nominal MMIO address; a core that
 * never calls this never receives any IRQ, no matter what
 * gic_distributor_init() or gic_enable_spi() did.
 */
void gic_percore_init(void);

/**
 * @brief Disable this core's GIC CPU interface and undo its timer/mailbox IRQ routing.
 * @note Per-core — the counterpart to gic_percore_init(), not gic_distributor_init().
 */
void gic_disable(void);

/**
 * @brief Enable a shared peripheral interrupt (SPI, INTID >= 32): set priority, route to core 0, configure level-sensitive, and enable it in the distributor.
 */
void gic_enable_spi(uint32_t intid, uint8_t priority);

/**
 * @brief Send a targeted IPI to one specific core via its ARM Local mailbox 0.
 * @note Not a GIC interrupt — GICD_IGROUPR (which decides Group 0/Secure vs.
 * Group 1/Non-secure for GIC interrupts, including SGIs) is confirmed
 * write-ignored from this Non-secure-only OS, so a GIC SGI can never be
 * delivered here; this bypasses the GIC entirely via the same ARM Local
 * mechanism CORE_TIMER_IRQCNTL already uses for the timer. See
 * companion_core/docs.md and companion_core_soft_reset_plan.md for the full
 * investigation. The target core must have called gic_percore_init() first.
 */
void gic_send_mailbox_ipi(uint32_t target_core);

#endif
