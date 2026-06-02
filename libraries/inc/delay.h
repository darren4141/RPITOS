#ifndef DELAY_H
#define DELAY_H

#include "status.h"
#include <stdint.h>

StatusCode delay_init(volatile uint64_t *p_tick_count);

void delay_ms(uint64_t ticks);

#endif
