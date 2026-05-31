#include "delay.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "uart.h"

#include <stdbool.h>
#include <stdint.h>

static volatile bool gpio_state = true;

static uint32_t clk_freq;
static uint32_t hz = 1000;
static uint32_t tick_count = 0;

static void delay(uint32_t count)
{
  while (count--) {
    __asm__ volatile ("nop");
  }
}

void kmain(void)
{
  uart_init(UART_BAUDRATE_115200);

  uart_print("Starting main...\r\n");
  gpio_set_function(16, GPIO_FUNC_OUTPUT);
  gpio_on(16);

  uart_print("Initializing GIC...\r\n");
  gic_init();

  uart_print("Initializing General Timer...\r\n");
  gentimer_init(&clk_freq, hz);
  delay_init(&clk_freq);
  __asm__ volatile ("cpsie i" ::: "memory");

  while (1) {
    __asm__ volatile ("nop");
    gpio_off(16);
    delay_ms(1000);
    gpio_on(16);
    delay_ms(1000);
  }
}

#define BLINK_HZ 2   /* toggle GPIO at 2 Hz → visible 1 Hz blink */

void __attribute__((noinline)) timer_tick_handler(void)
{
  // Read current CVAL and advance by one interval
  uint32_t lo, hi;
  __asm__ volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (lo), "=r" (hi));         // CNTP_CVAL read

  uint64_t cval = ((uint64_t)hi << 32) | lo;
  cval += (clk_freq / hz);                                                      // advance by one interval

  uint32_t new_lo = (uint32_t)(cval & 0xFFFFFFFF);
  uint32_t new_hi = (uint32_t)(cval >> 32);
  __asm__ volatile ("mcrr p15, 2, %0, %1, c14" : : "r" (new_lo), "r" (new_hi)); // CNTP_CVAL write

  tick_count++;
  if (tick_count > 500) {
    tick_count = 0;
    gpio_state = !gpio_state;
    if (gpio_state) {
      uart_print("Blink!\r\n");
      gpio_on(16);
    }
    else {
      uart_print("Blink!\r\n");
      gpio_off(16);
    }
  }
}
