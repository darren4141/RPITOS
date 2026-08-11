// See README.md for the scenario, expected output, and why LOW busy-spins
// instead of using task_delay_ms while holding the mutex.

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

#define HOLD_MS  2000U    // how long LOW holds the mutex (busy work)
#define PRINT_MS 50U      // how often LOW prints its priority during the hold

static Mutex shared_mtx;

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static const uint32_t hz = 1000;

static TaskControlBlock *tcb_low = NULL;
static TaskControlBlock *tcb_mid = NULL;
static TaskControlBlock *tcb_high = NULL;

static UartConfig uart_config = {
  .tx_pin = 14,
  .rx_pin = 15,
  .alt_func = GPIO_FUNC_ALT0,
  .baudrate = UART_BAUDRATE_115200,
  .mode = UART_MODE_BUFFERED_TASK,
  .is_dma_enabled = true,
  .dma_channel = UART_DEFAULT_DMA_CHANNEL,
  .task_stack_words = 2048,
  .task_priority = TASK_PRIORITY_5,
};

static WatchdogConfig watchdog_config = {
  .timeout_s = 5,
  .policy = WATCHDOG_RESET_POLICY_FORCE_UPDATE,
  .tolerance = 2,
  .confirm_delay_ms = 1000,
};

// LOW (priority 1): holds the mutex and busy-spins, printing its live priority
// every PRINT_MS ms so the boost from HIGH is visible in the output.
void low_task(void *params)
{
  (void)params;
  while (1) {
    uart_printf("[%5u] LOW : acquiring mutex\r\n",
                (uint32_t)scheduler_get_tick_count());

    mutex_lock(&shared_mtx, -1);

    uart_printf("[%5u] LOW : acquired (priority=%u base=%u) — starting %u ms work\r\n",
                (uint32_t)scheduler_get_tick_count(),
                tcb_low->priority, tcb_low->base_priority, HOLD_MS);

    // Busy-spin, printing priority every PRINT_MS ms.
    // When HIGH blocks on the mutex (~100 ms in), priority jumps from 1 to 3.
    uint64_t start = scheduler_get_tick_count();
    uint64_t next_print = start + PRINT_MS;
    uint64_t deadline = start + HOLD_MS;

    while (scheduler_get_tick_count() < deadline) {
      if (scheduler_get_tick_count() >= next_print) {
        uart_printf("[%5u] LOW : working... (priority=%u base=%u)\r\n",
                    (uint32_t)scheduler_get_tick_count(),
                    tcb_low->priority, tcb_low->base_priority);
        next_print += PRINT_MS;
      }
    }

    uart_printf("[%5u] LOW : releasing  (priority=%u base=%u)\r\n",
                (uint32_t)scheduler_get_tick_count(),
                tcb_low->priority, tcb_low->base_priority);

    mutex_unlock(&shared_mtx);

    uart_printf("[%5u] LOW : released   (priority=%u base=%u)\r\n",
                (uint32_t)scheduler_get_tick_count(),
                tcb_low->priority, tcb_low->base_priority);

    // Sleep to let HIGH and MID complete their current cycles cleanly.
    task_delay_ms(1000);
  }
}

// MID (priority 2): prints a tick every 50 ms and never touches the mutex.
// These prints go silent during the window where HIGH is blocked and LOW is
// boosted above MID.
void mid_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    count++;
    uart_printf("[%5u] MID : tick %u\r\n",
                (uint32_t)scheduler_get_tick_count(), count);
    task_delay_ms(50);
  }
}

// HIGH (priority 3): sleeps 100 ms so LOW can acquire the mutex first, then
// contends on it.  The moment HIGH blocks, mutex_lock boosts LOW to priority 3.
void high_task(void *params)
{
  (void)params;
  uint32_t round = 0;

  task_delay_ms(100);   // let LOW acquire the mutex first

  while (1) {
    round++;
    uart_printf("[%5u] HIGH: [round %u] blocking on mutex"
                " — LOW should be boosted to priority 3 now\r\n",
                (uint32_t)scheduler_get_tick_count(), round);

    mutex_lock(&shared_mtx, -1);

    uart_printf("[%5u] HIGH: [round %u] acquired mutex ✓\r\n",
                (uint32_t)scheduler_get_tick_count(), round);

    mutex_unlock(&shared_mtx);

    // Wait long enough for LOW to start a new cycle before we contend again.
    task_delay_ms(1300);
  }
}

void kmain(void)
{
  gpio_set_function(16, GPIO_FUNC_OUTPUT);
  jtag_gpio_init();

  uart_init(&uart_config);
  uart_print("\r\n=== priority inheritance mutex demo ===\r\n"
             "LOW=priority1  MID=priority2  HIGH=priority3\r\n"
             "LOW holds mutex and busy-spins for 300 ms.\r\n"
             "HIGH blocks ~100 ms into that window.\r\n"
             "With inheritance: LOW is boosted to priority3 when HIGH blocks,\r\n"
             "MID goes silent until LOW releases.  Timestamps in ms.\r\n"
             "To observe priority inversion: change mutex_set_inheritance to 0.\r\n\r\n");

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  mutex_init(&shared_mtx, "shared_mtx");
  mutex_set_inheritance(&shared_mtx, 1);   // set to 0 to see uninherited inversion

  task_create(low_task, 4096, TASK_PRIORITY_1, NULL, "low", &tcb_low);
  task_create(mid_task, 2048, TASK_PRIORITY_2, NULL, "mid", &tcb_mid);
  task_create(high_task, 2048, TASK_PRIORITY_3, NULL, "high", &tcb_high);

  software_timer_init();
  software_timer_start();

  watchdog_init(&watchdog_config);
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
