#ifndef GENTIMER_H
#define GENTIMER_H

#include <stdint.h>

#define HZ 1000

void gentimer_init(uint32_t *clk_freq, uint32_t hz);

#endif