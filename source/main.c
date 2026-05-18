#include <stdint.h>
#include "gpio.h"

static void delay(uint32_t count)
{
    while (count--)
        __asm__ volatile("nop");
}

void kmain(void)
{
    gpio_set_output(16);

    while (1) {
        gpio_on(16);
        delay(0x3D09000);
        gpio_off(16);
        delay(0x3D09000);
    }
}
