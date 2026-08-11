// See README.md for the timer schedule and what this sample demonstrates.

#include "boot_flags.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "reset.h"
#include "scheduler.h"
#include "software_timer.h"
#include "task.h"
#include "uart.h"
#include "watchdog.h"

#include <stdbool.h>
#include <stdint.h>

#define LED_PIN 16U

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static const uint32_t hz = 1000;   // 1 kHz tick → 1 tick == 1 ms

static TaskControlBlock *tcb_demo = NULL;
static TaskControlBlock *tcb_spawner = NULL;

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

// Software timers live in static storage — the kernel does no dynamic allocation.
static SoftwareTimer heartbeat_timer;   // periodic  250 ms — toggles the LED
static SoftwareTimer fast_timer;        // periodic  500 ms
static SoftwareTimer medium_timer;      // periodic 1000 ms
static SoftwareTimer slow_timer;        // periodic 2000 ms
static SoftwareTimer oneshot_timer;     // one-shot  5000 ms

// ── Timer callbacks ─────────────────────────────────────────────────────────
// Each receives the fired SoftwareTimer as `arg` (unused here — one callback
// per timer).

static void heartbeat_cb(void *arg)
{
  (void)arg;
  static bool led_on = false;
  led_on = !led_on;
  if (led_on) {
    gpio_on(LED_PIN);
  }
  else {
    gpio_off(LED_PIN);
  }
}

static void fast_cb(void *arg)
{
  (void)arg;
  uart_printf("[%6u ms] fast     - 500 ms periodic\r\n", (uint32_t)scheduler_get_tick_count());
}

static void medium_cb(void *arg)
{
  (void)arg;
  uart_printf("[%6u ms] medium   - 1 s periodic\r\n", (uint32_t)scheduler_get_tick_count());
}

static void slow_cb(void *arg)
{
  (void)arg;
  uart_printf("[%6u ms] slow     - 2 s periodic\r\n", (uint32_t)scheduler_get_tick_count());
}

static void oneshot_cb(void *arg)
{
  (void)arg;
  uart_printf("[%6u ms] one-shot - fires once, then done\r\n", (uint32_t)scheduler_get_tick_count());
}

// ── One-shot spawner ─────────────────────────────────────────────────────────
// Recycles a fixed pool of statically-allocated one-shot timers (see README).

#define ONESHOT_POOL_SIZE 8U

static SoftwareTimer oneshot_pool[ONESHOT_POOL_SIZE];

static void spawned_oneshot_cb(void *arg)
{
  SoftwareTimer *t = (SoftwareTimer *)arg;
  uint32_t slot = (uint32_t)(t - oneshot_pool);
  uart_printf("[%6u ms]   spawned one-shot fired (slot %u)\r\n",
              (uint32_t)scheduler_get_tick_count(), slot);
}

static void spawner_task(void *params)
{
  (void)params;

  uint32_t next = 0U;      // round-robin start point for the free-slot scan
  uint32_t period = 300U;  // walked over a range so expiries interleave

  while (1) {
    SoftwareTimer *slot = NULL;
    for (uint32_t i = 0U; i < ONESHOT_POOL_SIZE; i++) {
      uint32_t idx = (next + i) % ONESHOT_POOL_SIZE;
      if (oneshot_pool[idx].list_id == TIMER_LIST_NONE) {
        slot = &oneshot_pool[idx];
        next = (idx + 1U) % ONESHOT_POOL_SIZE;
        break;
      }
    }

    if (slot != NULL) {
      // Scheduler is running, so create() arms the one-shot immediately.
      software_timer_create(slot, period, spawned_oneshot_cb, TIMER_MODE_ONE_SHOT);
      period = 200U + ((period + 173U) % 600U);   // 200..799 ms, cycles around
    }
    else {
      uart_print("[spawn] pool full — waiting for one-shots to fire\r\n");
    }

    task_delay_ms(2000U);
  }
}

// ── Demo control task ────────────────────────────────────────────────────────
// Arms all five timers, then exercises stop()/reset() at 8 s (see README).

static void demo_task(void *params)
{
  (void)params;

  uart_print("[demo] arming software timers...\r\n");

  software_timer_create(&heartbeat_timer, 250U, heartbeat_cb, TIMER_MODE_PERIODIC);
  software_timer_create(&fast_timer, 500U, fast_cb, TIMER_MODE_PERIODIC);
  software_timer_create(&medium_timer, 1000U, medium_cb, TIMER_MODE_PERIODIC);
  software_timer_create(&slow_timer, 2000U, slow_cb, TIMER_MODE_PERIODIC);
  software_timer_create(&oneshot_timer, 5000U, oneshot_cb, TIMER_MODE_ONE_SHOT);

  uart_print("[demo] all five timers armed\r\n");

  // After 8 s: stop the fast timer (its prints cease) and restart the slow
  // timer's period from now — demonstrating software_timer_stop/reset.
  task_delay_ms(8000U);
  software_timer_stop(&fast_timer);
  uart_print("[demo] fast timer STOPPED\r\n");
  software_timer_reset(&slow_timer);
  uart_print("[demo] slow timer RESET (period restarts from now)\r\n");

  while (1) {
    task_delay_ms(60000U);
  }
}

void kmain(void)
{
  gpio_set_function(LED_PIN, GPIO_FUNC_OUTPUT);
  jtag_gpio_init();

  uart_init(&uart_config);
  uart_print("\r\n=== software timer demo ===\r\n"
             "heartbeat 250ms toggles the LED; fast/medium/slow print at\r\n"
             "500ms / 1s / 2s; a one-shot fires once at 5s. At 8s the fast\r\n"
             "timer is stopped and the slow timer is reset.\r\n\r\n");

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  // Software-timer subsystem must be initialized/started before watchdog_task_start
  // (it arms a confirm timer) and before scheduler_start().
  software_timer_init();
  software_timer_start();

  task_create(demo_task, 2048, TASK_PRIORITY_1, NULL, "demo", &tcb_demo);
  task_create(spawner_task, 2048, TASK_PRIORITY_2, NULL, "spawner", &tcb_spawner);

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
