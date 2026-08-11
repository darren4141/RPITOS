// See README.md for what this sample demonstrates.

#include "companion_core.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "scheduler.h"
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
static TaskControlBlock *tcb_uart2 = NULL;

// ── Core 1 — its own scheduler, own tick source, three independent blink tasks
static volatile uint32_t core1_clk_freq;
static volatile uint64_t core1_tick_count = 0;

// is_dma_enabled stays false here (was -DUART_TX_DMA=0) — see config.mk:
// this sample was isolating watchdog/software_timer presence as the
// multicore-hang differentiator, not the TX path.
static UartConfig uart_config = {
  .tx_pin = 14,
  .rx_pin = 15,
  .alt_func = GPIO_FUNC_ALT0,
  .baudrate = UART_BAUDRATE_115200,
  .mode = UART_MODE_BUFFERED_TASK,
  .is_dma_enabled = false,
  .task_stack_words = 2048,
  .task_priority = TASK_PRIORITY_5,
};

static CompanionCoreContext cc_ctx;

static WatchdogConfig watchdog_config = {
  .timeout_s = 5,
  .policy = WATCHDOG_RESET_POLICY_FORCE_UPDATE,
  .tolerance = 1,
  .confirm_delay_ms = 1000,
  .multicore_mode = true,
  .companion_core_ctx = &cc_ctx,
};

// Each core 1 task also uart_printf()s on its own toggle — three more UART
// producers alongside core 0's two (see README's uart_buf_lock stress note).
static void led16_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    gpio_on(LED_PIN_16);
    uart_printf("core 1 task16: message %u @ %u ms\r\n", count++,
                (uint32_t)scheduler_get_tick_count());
    task_delay_ms(500U);
    gpio_off(LED_PIN_16);
    task_delay_ms(500U);
  }
}

static void led20_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    gpio_on(LED_PIN_20);
    uart_printf("core 1 task20: message %u @ %u ms\r\n", count++,
                (uint32_t)scheduler_get_tick_count());
    task_delay_ms(250U);
    gpio_off(LED_PIN_20);
    task_delay_ms(250U);
  }
}

static void led21_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    gpio_on(LED_PIN_21);
    uart_printf("core 1 task21: message %u @ %u ms\r\n", count++,
                (uint32_t)scheduler_get_tick_count());
    task_delay_ms(125U);
    gpio_off(LED_PIN_21);
    task_delay_ms(125U);
  }
}

static void core1_kmain(void)
{
  uart_tx_raw('[');uart_tx_raw('R');uart_tx_raw('1');uart_tx_raw(']');   // DEBUG: core1_kmain entered

  gpio_set_function(LED_PIN_16, GPIO_FUNC_OUTPUT);
  gpio_set_function(LED_PIN_20, GPIO_FUNC_OUTPUT);
  gpio_set_function(LED_PIN_21, GPIO_FUNC_OUTPUT);
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('0');uart_tx_raw(']'); // DEBUG: gpio done

  gic_percore_init();
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('1');uart_tx_raw(']'); // DEBUG: gic_percore_init done

  gentimer_init(&core1_clk_freq, hz);
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('2');uart_tx_raw(']'); // DEBUG: gentimer_init done

  scheduler_init(1U, &core1_clk_freq, hz, &core1_tick_count);
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('3');uart_tx_raw(']'); // DEBUG: scheduler_init done

  watchdog_core_task_start();

  TaskControlBlock *tcb;
  task_create(led16_task, 2048, TASK_PRIORITY_1, NULL, "led16", &tcb);
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('1');uart_tx_raw(']'); // DEBUG: task16 created
  task_create(led20_task, 2048, TASK_PRIORITY_1, NULL, "led20", &tcb);
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('2');uart_tx_raw(']'); // DEBUG: task20 created
  task_create(led21_task, 2048, TASK_PRIORITY_1, NULL, "led21", &tcb);
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('3');uart_tx_raw(']'); // DEBUG: task21 created

  uart_tx_raw('[');uart_tx_raw('R');uart_tx_raw('2');uart_tx_raw(']'); // DEBUG: tasks created, about to start scheduler

  __asm__ volatile ("cpsie i" ::: "memory");
  scheduler_start();                                                   // never returns
}

// Core 0 UART tasks — two independent producers at different periods.
static void core0_uart_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    uart_printf("core 0 taskA: message %u @ %u ms\r\n", count++,
                (uint32_t)scheduler_get_tick_count());
    task_delay_ms(1000U);
  }
}

static void core0_uart_task2(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    uart_printf("core 0 taskB: message %u @ %u ms\r\n", count++,
                (uint32_t)scheduler_get_tick_count());
    task_delay_ms(700U);
  }
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(&uart_config);
  uart_print("\r\n=== multicore_blink (BMP) ===\r\n"
             "core 0: RTOS, 2 tasks printing periodic UART messages\r\n"
             "core 1: its own RTOS scheduler, 3 tasks blinking GPIO 16/20/21"
             " and each printing over UART\r\n\r\n");

  companion_core_init(&cc_ctx);

  watchdog_init(&watchdog_config);

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  software_timer_init();
  software_timer_start();

  task_create(core0_uart_task, 2048, TASK_PRIORITY_1, NULL, "uart_a", &tcb_uart);
  task_create(core0_uart_task2, 2048, TASK_PRIORITY_1, NULL, "uart_b", &tcb_uart2);

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
  if (companion_core_start(1U, core1_kmain) == E_OK) {
    uart_print("core 0: released core 1\r\n");
  }
  else {
    uart_print("core 0: companion_core_start failed\r\n");
  }

  __asm__ volatile ("cpsie i" ::: "memory");

  scheduler_start();
}
