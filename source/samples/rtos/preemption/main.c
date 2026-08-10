#include "boot_flags.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "mutex.h"
#include "reset.h"
#include "scheduler.h"
#include "software_timer.h"
#include "task.h"
#include "uart.h"
#include "watchdog.h"

#include <stdint.h>

#define HOLD_MS  300U
#define PRINT_MS 1U

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static const uint32_t hz = 1000;

static TaskControlBlock *tcb_low = NULL;
static TaskControlBlock *tcb_mid = NULL;
static TaskControlBlock *tcb_high = NULL;

// LOW (priority 1): busy-spins for HOLD_MS printing a counter every PRINT_MS.
void low_task(void *params)
{
  (void)params;
  task_delay_ms(2000);
  uint32_t counter = 0;


  while (1) {
    uint64_t start = scheduler_get_tick_count();
    uint64_t deadline = start + HOLD_MS;
    uint64_t next_print = start + PRINT_MS;

    while (scheduler_get_tick_count() < deadline) {
      if (scheduler_get_tick_count() >= next_print) {
        counter++;
        uart_printf("[%5u] LOW\r\n",
                    counter);
        next_print += PRINT_MS;
      }
    }

    counter = 0;

    task_delay_ms(10U);
  }
}

// MID (priority 2): prints a tick every 50 ms.
void mid_task(void *params)
{
  task_delay_ms(2000);

  (void)params;
  uint32_t count = 0;
  while (1) {
    count++;
    uart_printf("[%5u] MID : tick %u\r\n",
                (uint32_t)scheduler_get_tick_count(), count);
    task_delay_ms(50);
  }
}

// HIGH (priority 3): prints every 1300 ms.
void high_task(void *params)
{
  (void)params;
  uint32_t round = 0;

  while (1) {
    round++;
    uart_printf("[%5u] HIGH\r\n",
                (uint32_t)scheduler_get_tick_count());

    task_delay_ms(1300);
  }
}

void kmain(void)
{
  gpio_set_function(16, GPIO_FUNC_OUTPUT);
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_print("\r\n=== priority inheritance mutex demo ===\r\n"
             "LOW=priority1  MID=priority2  HIGH=priority3\r\n"
             "LOW holds mutex and busy-spins for 300 ms.\r\n"
             "HIGH blocks ~100 ms into that window.\r\n"
             "With inheritance: LOW is boosted to priority3 when HIGH blocks,\r\n"
             "MID goes silent until LOW releases.  Timestamps in ms.\r\n"
             "To observe priority inversion: change mutex_set_inheritance to 0.\r\n\r\n");

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  task_create(low_task, 2048, TASK_PRIORITY_1, NULL, "low", &tcb_low);
  task_create(mid_task, 2048, TASK_PRIORITY_2, NULL, "mid", &tcb_mid);
  task_create(high_task, 2048, TASK_PRIORITY_3, NULL, "high", &tcb_high);

  software_timer_init();
  software_timer_start();

  watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 2, 1000);
  watchdog_task_start();

  gic_distributor_init();
  gic_percore_init();
  gentimer_init(&clk_freq, hz);
  // DFU recovery is wired up automatically now (scheduler_init() + uart_task_start()) —
  // no per-app call needed. See dfu_trigger.h.

  // A/B trial boot: confirm this app slot now that init succeeded.
  wdt_meta_confirm_slot();

  __asm__ volatile ("cpsie i" ::: "memory");

  scheduler_start();
}
