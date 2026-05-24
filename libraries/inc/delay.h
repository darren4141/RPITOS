#ifndef DELAY_H
#define DELAY_H

#include "gentimer.h"
#include "status.h"
#include <stddef.h>
#include <stdint.h>

StatusCode delay_init(uint32_t *clk_freq);

void delay_ms(uint32_t ms);

#endif