#ifndef GENTIMER_H
#define GENTIMER_H

#include "status.h"
#include <stddef.h>
#include <stdint.h>

#define HZ 1000

StatusCode gentimer_init(uint32_t *clk_freq, uint32_t hz);

uint64_t read_cntpct(void);

#endif