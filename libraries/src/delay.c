#include "delay.h"

static uint32_t *p_freq;

StatusCode delay_init(uint32_t *clk_freq)
{
  if (clk_freq == NULL) {
    return ENULL;
  }
  p_freq = clk_freq;
}

void delay_ms(uint32_t ms)
{
}