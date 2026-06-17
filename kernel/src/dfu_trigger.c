#include "dfu_trigger.h"

static uint32_t shift_reg = 0;

void dfu_trigger_reset(void)
{
  shift_reg = 0;
}

int dfu_trigger_feed(uint8_t byte)
{
  shift_reg = (shift_reg << 8) | byte;
  return shift_reg == DFU_TRIGGER_KEY;
}
