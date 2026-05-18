#include <stdint.h>
#include "gpio.h"

#define GPIO_BASE 0xFE200000UL

void gpio_set_output(uint32_t pin)
{
    uint32_t reg   = pin / 10;
    uint32_t shift = (pin % 10) * 3;
    volatile uint32_t *gpfsel = (volatile uint32_t *)(GPIO_BASE + reg * 4);
    uint32_t val = *gpfsel;
    val &= ~(7u << shift);   /* clear the 3-bit function field */
    val |=  (1u << shift);   /* set to output (001) */
    *gpfsel = val;
}

void gpio_on(uint32_t pin)
{
    volatile uint32_t *gpset = (volatile uint32_t *)(GPIO_BASE + 28);
    *gpset = (1u << pin);
}

void gpio_off(uint32_t pin)
{
    volatile uint32_t *gpclr = (volatile uint32_t *)(GPIO_BASE + 40);
    *gpclr = (1u << pin);
}
