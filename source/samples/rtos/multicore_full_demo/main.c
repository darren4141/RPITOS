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
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('e');uart_tx_raw(']');   // DEBUG: core1_kmain entered
  gic_percore_init();
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('G');uart_tx_raw(']');   // DEBUG: gic_percore_init done
  gentimer_init(&core1_clk_freq, hz);
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('T');uart_tx_raw(']');   // DEBUG: gentimer_init done
  scheduler_init(1U, &core1_clk_freq, hz, &core1_tick_count);
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('S');uart_tx_raw(']');   // DEBUG: scheduler_init done

  TaskControlBlock *tcb;
  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr1", &tcb);
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('1');uart_tx_raw(']');   // DEBUG: mutex_counter_task created
  task_create(pong_task, 2048, TASK_PRIORITY_2, NULL, "pong", &tcb);
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('2');uart_tx_raw(']');   // DEBUG: pong_task created

  // NOT cpsie i here — p_task_control_block[core_id] is still NULL until
  // scheduler_switch_context() (called inside scheduler_start()) sets it; an
  // IRQ landing before that dereferences NULL in cntx_switch$. The first
  // task's own saved SPSR (0x13, IRQs enabled — task_init_stack()) turns
  // interrupts on safely via start_first_task()'s rfeia instead.
  uart_tx_raw('[');uart_tx_raw('1');uart_tx_raw('R');uart_tx_raw(']'); // DEBUG: about to scheduler_start
  scheduler_start();                                                   // never returns
}

static void core2_kmain(void)
{
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('e');uart_tx_raw(']'); // DEBUG: core2_kmain entered
  gic_percore_init();
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('G');uart_tx_raw(']'); // DEBUG: gic_percore_init done
  gentimer_init(&core2_clk_freq, hz);
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('T');uart_tx_raw(']'); // DEBUG: gentimer_init done
  scheduler_init(2U, &core2_clk_freq, hz, &core2_tick_count);
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('S');uart_tx_raw(']'); // DEBUG: scheduler_init done

  TaskControlBlock *tcb;
  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr2", &tcb);
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('1');uart_tx_raw(']'); // DEBUG: mutex_counter_task created
  task_create(queue_send_task, 2048, TASK_PRIORITY_2, NULL, "queue_send", &tcb);
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('2');uart_tx_raw(']'); // DEBUG: queue_send_task created

  // NOT cpsie i here — see core1_kmain's comment above the same point.
  uart_tx_raw('[');uart_tx_raw('2');uart_tx_raw('R');uart_tx_raw(']'); // DEBUG: about to scheduler_start
  scheduler_start();                                                   // never returns
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
  // uart_telemetry_init() is NOT called here — it now runs once, early, from
  // core 0's kmain(), before any core's scheduler_init() can trigger a
  // telemetry_send() (idle-task broadcast). scheduler_init(3, ...) below
  // does exactly that for core 3's own idle task, so the UART must already
  // be configured by the time this line runs, not after it.
  scheduler_init(3U, &core3_clk_freq, hz, &core3_tick_count);

  TaskControlBlock *tcb;
  task_create(telemetry_publisher_task, 2048, TASK_PRIORITY_1, NULL, "telemetry_pub", &tcb);

  // NOT cpsie i here — see core1_kmain's comment above the same point.
  scheduler_start();     // never returns
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_tx_raw('[');uart_tx_raw('U');uart_tx_raw('0');uart_tx_raw(']');   // DEBUG: uart_init done

#ifdef RTOS_TELEMETRY
  // Must run before ANY core's scheduler_init() — every core's idle-task
  // setup calls telemetry_send() (via telemetry_report_task_created()),
  // which hangs spinning on FR_TXFF if TELEMETRY_UART was never configured.
  // Previously this only ran inside core3_kmain(), well after core 0's own
  // scheduler_init(0, ...) below — that's what caused the crash-loop.
  uart_telemetry_init();
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('I');uart_tx_raw(']');   // DEBUG: uart_telemetry_init done
#endif

  uart_print("\r\n=== multicore_full_demo ===\r\n"
             "core 0: RTOS — mutex counter + semaphore ping + queue receiver\r\n"
             "core 1: RTOS — mutex counter + semaphore pong\r\n"
             "core 2: RTOS — mutex counter + queue sender\r\n"
             "core 3: telemetry publisher (dedicated, no app tasks)\r\n\r\n");

  watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 1);
  uart_tx_raw('[');uart_tx_raw('W');uart_tx_raw('I');uart_tx_raw(']');   // DEBUG: watchdog_init done

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_tx_raw('[');uart_tx_raw('S');uart_tx_raw('0');uart_tx_raw(']');   // DEBUG: scheduler_init(0) done
  uart_task_start();
  uart_tx_raw('[');uart_tx_raw('U');uart_tx_raw('T');uart_tx_raw(']');   // DEBUG: uart_task_start done

  software_timer_init();
  software_timer_start();
  uart_tx_raw('[');uart_tx_raw('S');uart_tx_raw('T');uart_tx_raw(']');   // DEBUG: software_timer init+start done

  mutex_init(&g_counter_mutex);
  semaphore_init(&g_ping_sem, 1, 0);
  queue_init(&g_msg_queue, 4, sizeof(uint32_t));
  uart_tx_raw('[');uart_tx_raw('S');uart_tx_raw('Y');uart_tx_raw(']'); // DEBUG: mutex/semaphore/queue init done

  task_create(mutex_counter_task, 2048, TASK_PRIORITY_1, NULL, "mutex_ctr0", &tcb_mutex0);
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('1');uart_tx_raw(']'); // DEBUG: mutex_counter_task created
  task_create(ping_task, 2048, TASK_PRIORITY_2, NULL, "ping", &tcb_ping);
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('2');uart_tx_raw(']'); // DEBUG: ping_task created
  task_create(queue_recv_task, 2048, TASK_PRIORITY_2, NULL, "queue_recv", &tcb_queue_recv);
  uart_tx_raw('[');uart_tx_raw('T');uart_tx_raw('3');uart_tx_raw(']'); // DEBUG: queue_recv_task created

  watchdog_task_start();
  uart_tx_raw('[');uart_tx_raw('W');uart_tx_raw('T');uart_tx_raw(']'); // DEBUG: watchdog_task_start done

  gic_distributor_init();                                              // global — must happen before any core is released
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('D');uart_tx_raw(']'); // DEBUG: gic_distributor_init done
  gic_percore_init();                                                  // core 0's own PPI30 + CPU-interface enable
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('P');uart_tx_raw(']'); // DEBUG: gic_percore_init done
  gentimer_init(&clk_freq, hz);
  uart_tx_raw('[');uart_tx_raw('G');uart_tx_raw('T');uart_tx_raw(']'); // DEBUG: gentimer_init done

  // DFU recovery is wired up automatically now (scheduler_init() + uart_task_start()) —
  // no per-app call needed. See dfu_trigger.h.

  // A/B trial boot: confirm this app slot now that init succeeded.
  wdt_meta_confirm_slot();
  uart_tx_raw('[');uart_tx_raw('C');uart_tx_raw('S');uart_tx_raw(']');   // DEBUG: wdt_meta_confirm_slot done

  for (volatile uint32_t i = 0; i < 20000U; i++) {
  }
  uart_tx_raw('[');uart_tx_raw('D');uart_tx_raw('L');uart_tx_raw(']');   // DEBUG: delay loop done

  // Release cores 1-3 into their own schedulers. Safe to call before core 0's
  // scheduler starts — it just publishes the entry and wakes the parked core.
  // Isolation series done: cpsie-before-scheduler_start() race fixed (removed
  // the early cpsie i on every core), and the task-name TCB backend was the
  // actual crash trigger (reverted — see md/client conversation). Cores 1-3
  // re-enabled to confirm the full multicore sample is clean end-to-end.
  if (companion_core_start(1U, core1_kmain) == E_OK) {
    uart_print("core 0: released core 1\r\n");
  }
  else {
    uart_print("core 0: companion_core_start(1) failed\r\n");
  }
  uart_tx_raw('[');uart_tx_raw('C');uart_tx_raw('1');uart_tx_raw(']');   // DEBUG: companion_core_start(1) call returned

  if (companion_core_start(2U, core2_kmain) == E_OK) {
    uart_print("core 0: released core 2\r\n");
  }
  else {
    uart_print("core 0: companion_core_start(2) failed\r\n");
  }
  uart_tx_raw('[');uart_tx_raw('C');uart_tx_raw('2');uart_tx_raw(']');   // DEBUG: companion_core_start(2) call returned

  if (companion_core_start(3U, core3_kmain) == E_OK) {
    uart_print("core 0: released core 3\r\n");
  }
  else {
    uart_print("core 0: companion_core_start(3) failed\r\n");
  }

  // NOT cpsie i here — p_task_control_block[core_id] is still NULL until
  // scheduler_switch_context() (called inside scheduler_start()) sets it; an
  // IRQ landing before that dereferences NULL in cntx_switch$ (root cause of
  // the crash-loop). The first task's own saved SPSR (0x13, IRQs enabled —
  // task_init_stack()) turns interrupts on safely via start_first_task()'s
  // rfeia instead.
  uart_tx_raw('[');uart_tx_raw('S');uart_tx_raw('R');uart_tx_raw(']');   // DEBUG: about to scheduler_start
  scheduler_start();
}
