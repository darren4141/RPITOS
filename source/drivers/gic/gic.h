#ifndef GIC_H
#define GIC_H

#include <stdint.h>

/**
 * @brief Initialize the GIC-400 distributor and CPU interface, and route the EL1 physical timer (PPI 30) to core 0.
 */
void gic_init();

/**
 * @brief Disable the GIC distributor and CPU interface, and undo the timer IRQ routing set up by gic_init().
 */
void gic_disable(void);

/**
 * @brief Enable a shared peripheral interrupt (SPI, INTID >= 32): set priority, route to core 0, configure level-sensitive, and enable it in the distributor.
 */
void gic_enable_spi(uint32_t intid, uint8_t priority);

#endif
