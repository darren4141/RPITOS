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
 * @brief Initialize this core's own GIC-400 CPU interface and enable its EL1 physical timer (PPI 30) IRQ.
 * @note Must be called once by *every* core that wants to take IRQs,
 * including core 0 — GICD_ISENABLER0/GICD_IPRIORITYR (for the PPI range),
 * GICC_CTLR, GICC_PMR, and CORE_TIMER_IRQCNTL are all per-core state on
 * GIC-400/BCM2711 despite living at one nominal MMIO address; a core that
 * never calls this never receives any IRQ, no matter what
 * gic_distributor_init() or gic_enable_spi() did.
 */
void gic_percore_init(void);

/**
 * @brief Disable this core's GIC CPU interface and undo its timer IRQ routing.
 * @note Per-core — the counterpart to gic_percore_init(), not gic_distributor_init().
 */
void gic_disable(void);

/**
 * @brief Enable a shared peripheral interrupt (SPI, INTID >= 32): set priority, route to core 0, configure level-sensitive, and enable it in the distributor.
 */
void gic_enable_spi(uint32_t intid, uint8_t priority);

#endif
