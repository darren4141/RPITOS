// See README.md for what this sample demonstrates.

#include "boot_flags.h"
#include "companion_core.h"
#include "dfu_trigger.h"
#include "emmc.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "i2c.h"
#include "jtag.h"
#include "mutex.h"
#include "pwm_pca9685.h"
#include "queue.h"
#include "scheduler.h"
#include "semaphore.h"
#include "software_timer.h"
#include "task.h"
#include "telemetry.h"
#include "uart.h"
#include "watchdog.h"

// wdt_meta_confirm_slot() (below) now clears wdt_reset_count on every healthy
// boot, so this only trips if the app crash-loops without ever reaching the
// confirm call two boots in a row.
#define WATCHDOG_TOLER 1

#include <stdbool.h>
#include <stdint.h>

static const uint32_t hz = 1000;        // 1 kHz tick, every core

// ── Shared cross-core synchronization objects — the whole point of this sample
static Mutex g_counter_mutex;           // contended by one task on every core
static volatile uint32_t g_shared_counter = 0;

static Semaphore g_ping_sem;            // core 0 gives, core 1 takes
static Queue g_msg_queue;               // core 2 sends, core 0 receives

// Round-trip producer/consumer handshake between core 1 and core 2 (see README).
static Semaphore g_data_ready_sem;      // core 1 gives once data's "produced", core 2 takes
static Semaphore g_processing_done_sem; // core 2 gives once "processed", core 1 takes

// ── Core 0
static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static TaskControlBlock *tcb_mutex0 = NULL;
static TaskControlBlock *tcb_ping = NULL;
static TaskControlBlock *tcb_queue_recv = NULL;
static TaskControlBlock *tcb_grind0 = NULL;
static TaskControlBlock *tcb_grind1 = NULL;
static TaskControlBlock *tcb_pca_blink = NULL;

// ── Core 1
static volatile uint32_t core1_clk_freq;
static volatile uint64_t core1_tick_count = 0;

// ── Core 2
static volatile uint32_t core2_clk_freq;
static volatile uint64_t core2_tick_count = 0;

// ── Core 3 — dedicated telemetry publisher, no app tasks (see core3_kmain)
static volatile uint32_t core3_clk_freq;
static volatile uint64_t core3_tick_count = 0;

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

// PCA9685 PWM driver — see pwm_pca9685/docs.md. GPIO4/5 ALT5 is I2C_CHANNEL_3
// per i2c/docs.md's channel table (a phase-1-confirmed channel).
static I2cConfig pca9685_i2c_config = {
  .sda_pin = 4,
  .scl_pin = 5,
  .alt_func = GPIO_FUNC_ALT5,
  .baudrate = I2C_BAUDRATE_STANDARD_100K,
};

static Pca9685Config pca9685_config = {
  .channel = I2C_CHANNEL_3,
  .i2c_addr = PCA9685_I2C_ADDR_DEFAULT,
  .pwm_freq_hz = 200,          // arbitrary — well within PCA9685_FREQ_HZ_MIN..MAX, fine for an on/off blink
};

#define PCA_BLINK_CHANNEL_A 4U // PCA9685 PWM output channel, not an I2C channel
#define PCA_BLINK_CHANNEL_B 5U

#ifdef RTOS_TELEMETRY
static UartConfig telemetry_uart_config = {
  .tx_pin = 12,
  .rx_pin = UART_PIN_NONE,
  .alt_func = GPIO_FUNC_ALT4,
  .baudrate = UART_BAUDRATE_921600,
  .mode = UART_MODE_BLOCKING,
};
#endif

static CompanionCoreContext cc_ctx;

static WatchdogConfig watchdog_config = {
  .timeout_s = 5,
  .policy = WATCHDOG_RESET_POLICY_FORCE_UPDATE,
  .tolerance = 1,
  .confirm_delay_ms = 1000,
  .multicore_mode = true,
  .companion_core_ctx = &cc_ctx,
};

// Busy-spins the calling task for roughly ms milliseconds — real RUNNING
// time, not a blocking delay (see README).
static void busy_work_ms(uint64_t ms)
{
  uint64_t start = scheduler_get_tick_count();
  volatile uint32_t churn = 0;
  while ((scheduler_get_tick_count() - start) < ms) {
    churn++;
  }
}

// One task, run identically on cores 0-2 — reads its own core id at runtime
// rather than being copy-pasted per core. Holds the mutex for a visible
// ~30 ms (busy_work_ms, not a delay) so waiters on other cores block for a
// real, observable duration.
static void mutex_counter_task(void *params)
{
  (void)params;
  uint32_t my_core = companion_core_id();
  while (1) {
    mutex_lock(&g_counter_mutex, -1);
    g_shared_counter++;
    uart_printf("core %u: shared counter now %u\r\n", my_core, g_shared_counter);
    busy_work_ms(30U);
    mutex_unlock(&g_counter_mutex);
    task_delay_ms(300U);
  }
}

// Core 0 — blinks PCA9685 PWM channels 4 and 5 together (full-on/full-off,
// not a soft fade) at 1 Hz. pwm_pca9685_init() itself must run in task
// context (it blocks on task_delay_ms() for the datasheet's post-SLEEP-clear
// settle time — see pwm_pca9685/docs.md), so it happens here at task start
// rather than in kmain(); i2c_channel_init() has no such requirement and
// already ran in kmain() before scheduler_start().
static void pca_blink_task(void *params)
{
  (void)params;

  StatusCode ret = pwm_pca9685_init(&pca9685_config);
  if (ret != E_OK) {
    uart_printf("core 0: pwm_pca9685_init failed (%d) — blink task parked\r\n", ret);
    while (1) {
      task_delay_ms(1000U);
    }
  }

  bool led_on = false;
  while (1) {
    if (led_on) {
      pwm_pca9685_set_channel_full_off(PCA_BLINK_CHANNEL_A);
      pwm_pca9685_set_channel_full_on(PCA_BLINK_CHANNEL_B);
    }
    else {
      pwm_pca9685_set_channel_full_on(PCA_BLINK_CHANNEL_A);
      pwm_pca9685_set_channel_full_off(PCA_BLINK_CHANNEL_B);
    }
    led_on = !led_on;
    task_delay_ms(500U);
  }
}

// CPU-bound filler task, one per app core (0-2) — see README.
static void grind_task(void *params)
{
  (void)params;
  while (1) {
    busy_work_ms(50U);
    task_delay_ms(15U);
  }
}

static void grind_task_2(void *params)
{
  (void)params;
  while (1) {
    busy_work_ms(50U);
    task_delay_ms(15U);
  }
}

// Core 0 — semaphore producer side.
static void ping_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    task_delay_ms(500U);
    semaphore_give(&g_ping_sem);
    uart_printf("core 0: ping %u sent\r\n", count++);
  }
}

// Core 1 — semaphore consumer side. Blocks until core 0's ping_task gives —
// demonstrates semaphore_take() correctly waking a task on a different core
// than the one that called semaphore_give().
static void pong_task(void *params)
{
  (void)params;
  uint32_t count = 0;
  while (1) {
    if (semaphore_take(&g_ping_sem, SEMAPHORE_TAKE_BLOCKING) == E_OK) {
      uart_printf("core 1: pong %u received\r\n", count++);
    }
  }
}

// Core 1 — "produces data" (busy work), hands it off, then blocks waiting for
// core 2 to finish "processing" before producing again (see README).
static void handshake_producer_task(void *params)
{
  (void)params;
  uint32_t batch = 0;
  while (1) {
    busy_work_ms(700U);   // "producing"
    uart_printf("core 1: produced batch %u, handing off\r\n", batch);
    semaphore_give(&g_data_ready_sem);

    semaphore_take(&g_processing_done_sem, SEMAPHORE_TAKE_BLOCKING);
    uart_printf("core 1: batch %u acked, producing next\r\n", batch);
    batch++;
  }
}

// Core 2 — blocks waiting for core 1's data, "processes" it (busy work,
// deliberately longer than the producer's), then hands the ack back.
static void handshake_consumer_task(void *params)
{
  (void)params;
  uint32_t batch = 0;
  while (1) {
    semaphore_take(&g_data_ready_sem, SEMAPHORE_TAKE_BLOCKING);
    uart_printf("core 2: processing batch %u\r\n", batch);
    busy_work_ms(1800U);   // "processing"
    uart_printf("core 2: batch %u done\r\n", batch);
    semaphore_give(&g_processing_done_sem);
    batch++;
  }
}

// Core 2 — queue producer side.
static void queue_send_task(void *params)
{
  (void)params;
  uint32_t msg = 0;
  while (1) {
    if (queue_send(&g_msg_queue, &msg, 200) == E_OK) {
      uart_printf("core 2: sent msg %u\r\n", msg);
      msg++;
    }
    else {
      uart_print("core 2: queue full, send timed out\r\n");
    }
    task_delay_ms(150U);
  }
}

// Core 0 — queue consumer side. Blocks until core 2's queue_send_task sends —
// demonstrates queue_recv() correctly waking a task on a different core than
// the one that called queue_send(). (Runs on core 0, not core 3 — core 3 is
// dedicated to telemetry, see core3_kmain.)
static void queue_recv_task(void *params)
{
  (void)params;
  uint32_t msg;
  while (1) {
    if (queue_recv(&g_msg_queue, &msg, SEMAPHORE_TAKE_BLOCKING) == E_OK) {
      uart_printf("core 0: received msg %u\r\n", msg);
    }
  }
}

static void core1_kmain(void)
{
  gic_percore_init();
  gentimer_init(&core1_clk_freq, hz);
  scheduler_init(1U, &core1_clk_freq, hz, &core1_tick_count);
  watchdog_core_task_start();

  TaskControlBlock *tcb;
  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr1", &tcb);
  task_create(pong_task, 2048, TASK_PRIORITY_2, NULL, "pong", &tcb);
  task_create(handshake_producer_task, 2048, TASK_PRIORITY_2, NULL, "hs_producer", &tcb);
  task_create(grind_task, 2048, TASK_PRIORITY_1, NULL, "grind1", &tcb);

  // No cpsie here: IRQs must stay off until scheduler_start() sets
  // p_task_control_block[core_id] — see md/client/device/instrumentation.md.
  scheduler_start();   // never returns
}

static void core2_kmain(void)
{
  gic_percore_init();
  gentimer_init(&core2_clk_freq, hz);
  scheduler_init(2U, &core2_clk_freq, hz, &core2_tick_count);
  watchdog_core_task_start();

  TaskControlBlock *tcb;
  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr2", &tcb);
  task_create(queue_send_task, 2048, TASK_PRIORITY_2, NULL, "queue_send", &tcb);
  task_create(handshake_consumer_task, 2048, TASK_PRIORITY_2, NULL, "hs_consumer", &tcb);
  task_create(grind_task, 2048, TASK_PRIORITY_1, NULL, "grind2", &tcb);

  // No cpsie here — see core1_kmain().
  scheduler_start();   // never returns
}

// Core 3 — dedicated telemetry publisher, no RTOS-under-test tasks (so it
// can't be starved by, or starve, cores 0-2). See instrumentation.md.
static void core3_kmain(void)
{
  gic_percore_init();
  gentimer_init(&core3_clk_freq, hz);
  // uart_channel_init(UART_CHANNEL_TELEMETRY, ...) (core 0's kmain()) must
  // already have run — every core's idle-task setup broadcasts over
  // telemetry during scheduler_init().
  scheduler_init(3U, &core3_clk_freq, hz, &core3_tick_count);
  watchdog_core_task_start();

  TaskControlBlock *tcb;
  task_create(telemetry_publisher_task, 2048, TASK_PRIORITY_1, NULL, "telemetry_pub", &tcb);

  // No cpsie here — see core1_kmain().
  scheduler_start();   // never returns
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(&uart_config);

  // No task-context requirement here (unlike pwm_pca9685_init() — see
  // pca_blink_task()), so this can run directly in kmain().
  i2c_channel_init(I2C_CHANNEL_3, &pca9685_i2c_config);

#ifdef RTOS_TELEMETRY
  // Must run before any core's scheduler_init() — see instrumentation.md.
  uart_channel_init(UART_CHANNEL_TELEMETRY, &telemetry_uart_config);
  telemetry_report_boot_stage_enter(BOOT_STAGE_APP);

  // Re-broadcast boot_flags here — the bootloader's one-shot PKT_BOOT_INFO
  // fires before the app exists, so a host connecting later would otherwise
  // miss it. See md/client/device/boot_init_tracking.md.
  telemetry_report_boot_info_boot_flags((uint8_t)boot_flags.reset_reason,
                                        (boot_flags.dfu_requested == DFU_REQUEST) ? 1U : 0U,
                                        (uint8_t)boot_flags.fw_crc_ok);
#endif

  uart_print("\r\n=== multicore_full_demo ===\r\n"
             "core 0: RTOS — mutex counter + semaphore ping + queue receiver + PCA9685 blink\r\n"
             "core 1: RTOS — mutex counter + semaphore pong\r\n"
             "core 2: RTOS — mutex counter + queue sender\r\n"
             "core 3: telemetry publisher (dedicated, no app tasks)\r\n\r\n");

  companion_core_init(&cc_ctx);

  watchdog_init(&watchdog_config);

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  software_timer_init();
  software_timer_start();

  mutex_init(&g_counter_mutex, "counter_mtx");
  semaphore_init(&g_ping_sem, 1, 0, "ping_sem");
  semaphore_init(&g_data_ready_sem, 1, 0, "data_ready_sem");
  semaphore_init(&g_processing_done_sem, 1, 0, "processing_done_sem");
  queue_init(&g_msg_queue, 4, sizeof(uint32_t), "msg_queue");

  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr0", &tcb_mutex0);
  task_create(ping_task, 2048, TASK_PRIORITY_2, NULL, "ping", &tcb_ping);
  task_create(queue_recv_task, 2048, TASK_PRIORITY_2, NULL, "queue_recv", &tcb_queue_recv);
  task_create(grind_task, 2048, TASK_PRIORITY_1, NULL, "grind0", &tcb_grind0);
  task_create(grind_task_2, 2048, TASK_PRIORITY_1, NULL, "grind1", &tcb_grind1);
  task_create(pca_blink_task, 2048, TASK_PRIORITY_1, NULL, "pca_blink", &tcb_pca_blink);

  watchdog_task_start();

  gic_distributor_init();   // global — must happen before any core is released
  gic_percore_init();       // core 0's own PPI30 + CPU-interface enable
#ifdef RTOS_TELEMETRY
  telemetry_report_boot_milestone(BOOT_MS_GIC_INIT_DONE, BOOT_STAGE_APP, 0);
#endif
  gentimer_init(&clk_freq, hz);
#ifdef RTOS_TELEMETRY
  telemetry_report_boot_milestone(BOOT_MS_GENTIMER_INIT_DONE, BOOT_STAGE_APP, 0);
#endif

#ifdef RTOS_TELEMETRY
  telemetry_report_boot_milestone(BOOT_MS_SLOT_CONFIRMED, BOOT_STAGE_APP, 0);
  // Same reasoning as the boot_flags broadcast above; wdt_meta is guaranteed
  // fresh here since wdt_meta_confirm_slot() above already read it.
  telemetry_report_boot_info_slot_state((wdt_meta.active_app_slot == APP_SLOT_B) ? 1U : 0U,
                                        (uint8_t)wdt_meta.app_slot_trial, wdt_meta.trial_boot_count,
                                        wdt_meta.wdt_reset_count, wdt_meta.wdt_reset_tolerance,
                                        (uint8_t)wdt_meta.wdt_reset_policy, (uint8_t)wdt_meta.wdt_reset_reason);
#endif

  for (volatile uint32_t i = 0; i < 20000U; i++) {
  }

  // Release cores 1-3 into their own schedulers. Safe to call before core 0's
  // scheduler starts — it just publishes the entry and wakes the parked core.
  if (companion_core_start(1U, core1_kmain) == E_OK) {
    uart_print("core 0: released core 1\r\n");
  }
  else {
    uart_print("core 0: companion_core_start(1) failed\r\n");
  }

  if (companion_core_start(2U, core2_kmain) == E_OK) {
    uart_print("core 0: released core 2\r\n");
  }
  else {
    uart_print("core 0: companion_core_start(2) failed\r\n");
  }

  if (companion_core_start(3U, core3_kmain) == E_OK) {
    uart_print("core 0: released core 3\r\n");
  }
  else {
    uart_print("core 0: companion_core_start(3) failed\r\n");
  }

  // No cpsie here — see core1_kmain().
  scheduler_start();
}
