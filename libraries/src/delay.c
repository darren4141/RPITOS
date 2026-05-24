#include "delay.h"

static uint32_t *p_freq;

StatusCode delay_init(uint32_t *clk_freq)
{
  if (clk_freq == NULL) {
    return E_INVALID_ARGS;
  }
  p_freq = clk_freq;

  return E_OK;
}

void delay_ms(uint32_t ms)
{
  uint64_t start = read_cntpct();

  uint64_t ticks = ((uint64_t)(*p_freq) * ms) / 1000ULL;

  while ((read_cntpct() - start) < ticks) {}
}