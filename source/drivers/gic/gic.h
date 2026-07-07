#ifndef GIC_H
#define GIC_H

#include <stdint.h>

void gic_init();
void gic_disable(void);

// Enable a shared peripheral interrupt (SPI, INTID >= 32): set priority, route
// to core 0, configure level-sensitive, and enable it in the distributor.
void gic_enable_spi(uint32_t intid, uint8_t priority);

#endif