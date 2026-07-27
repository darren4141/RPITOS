#include "gpio.h"

void gpio_set_function(uint32_t pin, GPIOFunc funct)
{
  uint32_t reg = pin / 10;
  uint32_t shift = (pin % 10) * 3;
  uint32_t val = GPIO->GPFSEL[reg];
  val &= ~(7U << shift);
  val |= ((uint32_t)funct << shift);
  GPIO->GPFSEL[reg] = val;
}

void gpio_set_pull(uint8_t pin, GPIOPull pull)
{
  uint8_t reg = pin / 16;
  uint8_t shift = (pin % 16) * 2;

  uint32_t val = GPIO->GPPUPPDN[reg];
  val &= ~(3U << shift);
  val |= ((uint32_t)pull << shift);
  GPIO->GPPUPPDN[reg] = val;
}

void gpio_on(uint32_t pin)
{
  GPIO->GPSET[pin / 32] = 1U << (pin % 32);
}

void gpio_off(uint32_t pin)
{
  GPIO->GPCLR[pin / 32] = 1U << (pin % 32);
}

uint8_t gpio_read(uint32_t pin)
{
  return (GPIO->GPLEV[pin / 32] >> (pin % 32)) & 1;
}