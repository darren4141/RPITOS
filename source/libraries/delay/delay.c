#include "delay.h"

#include <stddef.h>

static volatile uint64_t *s_tick_count;

StatusCode delay_init(volatile uint64_t *p_tick_count)
{
  if (p_tick_count == NULL) {
    return E_INVALID_ARGS;
  }
  s_tick_count = p_tick_count;

  return E_OK;
}

void delay_cycles(uint64_t cycles)
{
  for (volatile uint64_t i = 0; i < cycles; i++) {
    __asm__ volatile ("nop");
  }
}

void delay_ms(uint64_t ticks)
{
  uint64_t start = *s_tick_count;

  while ((*s_tick_count - start) < ticks) {}
}
