#include "gpio.h"
#include "jtag.h"

#define JTAG_TRST        22
#define JTAG_TDO         24
#define JTAG_TCK         25
#define JTAG_TDI         26
#define JTAG_TMS         27

#define CHIPCTL_BASE     0xFF800000
#define CHIPCTL_A        (*(volatile uint32_t *)(CHIPCTL_BASE + 0x00))
#define JTAG_ENABLE_MASK (1 << 21)

void jtag_gpio_init(void)
{
  CHIPCTL_A |= JTAG_ENABLE_MASK;

  gpio_set_pull(JTAG_TRST, GPIO_PULL_UP);
  gpio_set_pull(JTAG_TDO, GPIO_PULL_NONE);
  gpio_set_pull(JTAG_TCK, GPIO_PULL_NONE);
  gpio_set_pull(JTAG_TDI, GPIO_PULL_NONE);
  gpio_set_pull(JTAG_TMS, GPIO_PULL_UP);

  gpio_set_function(JTAG_TRST, GPIO_FUNC_ALT4);
  gpio_set_function(JTAG_TDO, GPIO_FUNC_ALT4);
  gpio_set_function(JTAG_TCK, GPIO_FUNC_ALT4);
  gpio_set_function(JTAG_TDI, GPIO_FUNC_ALT4);
  gpio_set_function(JTAG_TMS, GPIO_FUNC_ALT4);
}