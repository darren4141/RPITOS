/*
 * multicore_blink — minimal AMP demo
 *
 * Core 0 runs the RTOS: it starts the UART task and sends periodic UART
 * messages from a scheduled task. Core 0 also releases secondary core 1 (via
 * smp_start_core), which runs a bare, kernel-free loop blinking an LED on
 * GPIO 21.
 *
 * This proves the AMP bring-up path end to end: secondary release from the
 * bootstrap parking loop, per-core HYP-exit/cache-disable/stack (done in the
 * bootstrap startup), and a secondary touching a peripheral correctly.
 *
 * Scope: AMP, one secondary, no shared writable state, busy-loop delay on the
 * secondary (no per-core timer/GIC yet). See md/amp_multicore_plan.md.
 */

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

#define LED_PIN_CORE1 16U   // toggled by the bare loop on core 1

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static const uint32_t hz = 1000;   // 1 kHz tick

static TaskControlBlock *tcb_uart = NULL;

// ── Core 1 entry (AMP) ───────────────────────────────────────────────────────
// Runs forever on the secondary core. No kernel calls, no shared state; a
// busy-loop delay keeps it free of the timer/GIC so this milestone needs no
// per-core interrupt setup. The core's caches were disabled in the bootstrap
// secondary path, so these GPIO MMIO writes reach the pin.
static void core1_blink(void)
{
  gpio_set_function(LED_PIN_CORE1, GPIO_FUNC_OUTPUT);
  for ( ; ; ) {
    gpio_on(LED_PIN_CORE1);
    for (volatile uint32_t i = 0; i < 1000000U; i++) {
    }
    gpio_off(LED_PIN_CORE1);
    for (volatile uint32_t i = 0; i < 1000000U; i++) {
    }
  }
}

// ── Core 0 UART task (RTOS) ──────────────────────────────────────────────────
// Sends a periodic message over the UART task (DMA TX). uart_printf is task-safe.
static void core0_uart_task(void *params)
{
  (void)params;
  // gpio_set_function(LED_PIN_CORE1, GPIO_FUNC_OUTPUT);
  uint32_t count = 0;
  while (1) {
    uart_printf("core 0: message %u @ %u ms (stack free=%u words)\r\n", count++,
                (uint32_t)scheduler_get_tick_count(),
                task_stack_free_words(tcb_uart));
    task_delay_ms(1000U);
    // gpio_on(LED_PIN_CORE1);
    // for (volatile uint32_t i = 0; i < 200000U; i++) {
    // }
    // gpio_off(LED_PIN_CORE1);
    // for (volatile uint32_t i = 0; i < 200000U; i++) {
    // }
  }
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_print("\r\n=== multicore_blink (AMP) NEW APP===\r\n"
             "core 0: RTOS + periodic UART messages\r\n"
             "core 1: bare loop + LED on GPIO 16\r\n\r\n");

  scheduler_init(&clk_freq, hz, &tick_count);
  uart_task_start();

  // Software-timer subsystem must be initialized/started before watchdog_task_start
  // (it arms a confirm timer) and before schedulerStart(). Matches every other
  // RTOS sample — added here to test whether its absence was the differentiator
  // between multicore_blink (fails) and the other samples (all pass).
  software_timer_init();
  software_timer_start();

  task_create(core0_uart_task, 2048, TASK_PRIORITY_1, NULL, &tcb_uart);

  watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 2);
  watchdog_task_start();

  gic_init();
  gentimer_init(&clk_freq, hz);

  // Running-app DFU trigger: watch UART RX for DF 00 DF 00 and reboot into the
  // bootloader's DFU mode.
  dfu_trigger_reset();
  dfu_trigger_task_start();
  uart_rx_irq_enable(dfu_trigger_feed_isr);

  // A/B trial boot: confirm this app slot now that init succeeded.
  wdt_meta_confirm_slot();

  for (volatile uint32_t i = 0; i < 20000U; i++) {
  }

  // Release core 1 into its bare blink loop. Safe to call before the scheduler
  // starts — it just publishes the entry and wakes the parked core.
  // if (smp_start_core(1U, core1_blink) == E_OK) {
  // uart_print("core 0: released core 1\r\n");
  // }
  // else {
  // uart_print("core 0: smp_start_core failed\r\n");
  // }

  __asm__ volatile ("cpsie i" ::: "memory");

  schedulerStart();
}
