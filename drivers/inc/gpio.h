#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

void gpio_set_output(uint32_t pin);
void gpio_on(uint32_t pin);
void gpio_off(uint32_t pin);

#endif
