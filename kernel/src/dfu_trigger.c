#include "dfu_trigger.h"

volatile uint32_t dfu_pending = 0;
static uint32_t shift_reg = 0;

void dfu_trigger_reset(void)
{
  shift_reg = 0;
}

int dfu_trigger_get_val()
{
  return shift_reg;
}

int dfu_trigger_feed(uint8_t byte)
{
  shift_reg = (shift_reg << 8) | byte;
  return shift_reg == DFU_TRIGGER_KEY;
}
