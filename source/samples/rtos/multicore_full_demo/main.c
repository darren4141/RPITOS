// See README.md for what this sample demonstrates.

#include "companion_core.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "mutex.h"
#include "queue.h"
#include "scheduler.h"
#include "semaphore.h"
#include "software_timer.h"
#include "task.h"
#include "telemetry.h"
#include "uart.h"
#include "watchdog.h"

#include <stdint.h>

static const uint32_t hz = 1000;   // 1 kHz tick, every core

// ── Shared cross-core synchronization objects — the whole point of this sample
static Mutex g_counter_mutex;      // contended by one task on every core
static volatile uint32_t g_shared_counter = 0;

static Semaphore g_ping_sem;       // core 0 gives, core 1 takes
static Queue g_msg_queue;          // core 2 sends, core 0 receives — core 3 is dedicated to telemetry, see below

// ── Core 0
static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static TaskControlBlock *tcb_mutex0 = NULL;
static TaskControlBlock *tcb_ping = NULL;
static TaskControlBlock *tcb_queue_recv = NULL;

// ── Core 1
static volatile uint32_t core1_clk_freq;
static volatile uint64_t core1_tick_count = 0;

// ── Core 2
static volatile uint32_t core2_clk_freq;
static volatile uint64_t core2_tick_count = 0;

// ── Core 3 — dedicated telemetry publisher, no app tasks (see core3_kmain)
static volatile uint32_t core3_clk_freq;
static volatile uint64_t core3_tick_count = 0;

// One task, run identically on all four cores — reads its own core id at
// runtime rather than being copy-pasted per core. Demonstrates mutex_lock()/
// mutex_unlock() genuinely serializing a shared resource across four
// independent per-core schedulers: the printed sequence should show every
// value exactly once, in order, regardless of which core produced it.
static void mutex_counter_task(void *params)
{
  (void)params;
  uint32_t my_core = companion_core_id();
  while (1) {
    mutex_lock(&g_counter_mutex, -1);
    g_shared_counter++;
    uart_printf("core %u: shared counter now %u\r\n", my_core, g_shared_counter);
    mutex_unlock(&g_counter_mutex);
    task_delay_ms(300U);
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
    task_delay_ms(400U);
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

  TaskControlBlock *tcb;
  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr1", &tcb);
  task_create(pong_task, 2048, TASK_PRIORITY_2, NULL, "pong", &tcb);

  __asm__ volatile ("cpsie i" ::: "memory");
  scheduler_start();     // never returns
}

static void core2_kmain(void)
{
  gic_percore_init();
  gentimer_init(&core2_clk_freq, hz);
  scheduler_init(2U, &core2_clk_freq, hz, &core2_tick_count);

  TaskControlBlock *tcb;
  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr2", &tcb);
  task_create(queue_send_task, 2048, TASK_PRIORITY_2, NULL, "queue_send", &tcb);

  __asm__ volatile ("cpsie i" ::: "memory");
  scheduler_start();     // never returns
}

// Core 3 — dedicated telemetry publisher. No RTOS-under-test tasks here on
// purpose: this core exists solely to drain telemetry_send()'s output onto
// UART0 at a steady rate, so it can never be starved by (or itself starve)
// the mutex/semaphore/queue demo running on cores 0-2. See
// md/client/device_instrumentation.md's "Publisher task" section — this is
// the dedicated-core placement option.
static void core3_kmain(void)
{
  gic_percore_init();
  gentimer_init(&core3_clk_freq, hz);
  scheduler_init(3U, &core3_clk_freq, hz, &core3_tick_count);

  uart_telemetry_init();   // dedicated UART3/GPIO4 — separate wire from UART0's console traffic

  TaskControlBlock *tcb;
  task_create(telemetry_publisher_task, 2048, TASK_PRIORITY_1, NULL, "telemetry_pub", &tcb);

  __asm__ volatile ("cpsie i" ::: "memory");
  scheduler_start();     // never returns
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_print("\r\n=== multicore_full_demo ===\r\n"
             "core 0: RTOS — mutex counter + semaphore ping + queue receiver\r\n"
             "core 1: RTOS — mutex counter + semaphore pong\r\n"
             "core 2: RTOS — mutex counter + queue sender\r\n"
             "core 3: telemetry publisher (dedicated, no app tasks)\r\n\r\n");

  watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 1);

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  software_timer_init();
  software_timer_start();

  mutex_init(&g_counter_mutex);
  semaphore_init(&g_ping_sem, 1, 0);
  queue_init(&g_msg_queue, 4, sizeof(uint32_t));

  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr0", &tcb_mutex0);
  task_create(ping_task, 2048, TASK_PRIORITY_2, NULL, "ping", &tcb_ping);
  task_create(queue_recv_task, 2048, TASK_PRIORITY_2, NULL, "queue_recv", &tcb_queue_recv);

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

  __asm__ volatile ("cpsie i" ::: "memory");

  scheduler_start();
}
