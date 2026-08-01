// See README.md for what this sample demonstrates.

#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "scheduler.h"
#include "smp.h"
#include "software_timer.h"
#include "task.h"
#include "uart.h"
#include "watchdog.h"

#include <stdint.h>

#define LED_PIN_16 16U   // core 1 task, 500 ms period
#define LED_PIN_20 20U   // core 1 task, 250 ms period
#define LED_PIN_21 21U   // core 1 task, 125 ms period

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static const uint32_t hz = 1000;   // 1 kHz tick

static TaskControlBlock *tcb_uart = NULL;

// ── Core 1 — its own scheduler, own tick source, three independent blink tasks
static volatile uint32_t core1_clk_freq;
static volatile uint64_t core1_tick_count = 0;

static void led16_task(void *params)
{
  (void)params;
  while (1) {
    gpio_on(LED_PIN_16);
    task_delay_ms(500U);
    gpio_off(LED_PIN_16);
    task_delay_ms(500U);
  }
}

static void led20_task(void *params)
{
  (void)params;
  while (1) {
    gpio_on(LED_PIN_20);
    task_delay_ms(250U);
    gpio_off(LED_PIN_20);
    task_delay_ms(250U);
  }
}

static void led21_task(void *params)
{
  (void)params;
  while (1) {
    gpio_on(LED_PIN_21);
    task_delay_ms(125U);
    gpio_off(LED_PIN_21);
    task_delay_ms(125U);
  }
}

static void core1_kmain(void)
{
  gpio_set_function(LED_PIN_16, GPIO_FUNC_OUTPUT);
  gpio_set_function(LED_PIN_20, GPIO_FUNC_OUTPUT);
  gpio_set_function(LED_PIN_21, GPIO_FUNC_OUTPUT);

  gic_percore_init();
  gentimer_init(&core1_clk_freq, hz);
  scheduler_init(1U, &core1_clk_freq, hz, &core1_tick_count);

  TaskControlBlock *tcb;
  task_create(led16_task, 2048, TASK_PRIORITY_1, NULL, &tcb);
  task_create(led20_task, 2048, TASK_PRIORITY_1, NULL, &tcb);
  task_create(led21_task, 2048, TASK_PRIORITY_1, NULL, &tcb);

  __asm__ volatile ("cpsie i" ::: "memory");
  scheduler_start();     // never returns
}

// Core 0 UART task (RTOS)
// Sends a periodic message over the UART task (DMA TX). uart_printf is task-safe.
static void core0_uart_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    uart_printf("core 0: message %u @ %u ms\r\n", count++,
                (uint32_t)scheduler_get_tick_count());
    task_delay_ms(1000U);
  }
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_print("\r\n=== multicore_blink (BMP) ===\r\n"
             "core 0: RTOS + periodic UART messages\r\n"
             "core 1: its own RTOS scheduler, 3 tasks blinking GPIO 16/20/21\r\n\r\n");

  watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 1);

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  software_timer_init();
  software_timer_start();

  task_create(core0_uart_task, 2048, TASK_PRIORITY_1, NULL, &tcb_uart);

  watchdog_task_start();

  gic_distributor_init();   // global — must happen before any core is released
  gic_percore_init();       // core 0's own PPI30 + CPU-interface enable
  gentimer_init(&clk_freq, hz);

  // DFU recovery is wired up automatically now (scheduler_init() + uart_task_start()) —
  // no per-app call needed. See dfu_trigger.h.

  // A/B trial boot: confirm this app slot now that init succeeded.
  wdt_meta_confirm_slot();

  for (volatile uint32_t i = 0; i < 20000U; i++) {
  }

  // Release core 1 into its own scheduler. Safe to call before core 0's
  // scheduler starts — it just publishes the entry and wakes the parked core.
  if (smp_start_core(1U, core1_kmain) == E_OK) {
    uart_print("core 0: released core 1\r\n");
  }
  else {
    uart_print("core 0: smp_start_core failed\r\n");
  }

  __asm__ volatile ("cpsie i" ::: "memory");

  scheduler_start();
}
