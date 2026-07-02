#include "boot_flags.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "mutex.h"
#include "reset.h"
#include "scheduler.h"
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
static TaskControlBlock *tcb_dfu = NULL;

void dfu_trigger_task(void *params)
{
  (void)params;
  uart_print("Safety window\r\n");
  while (1) {
    uart_print(".");
    if (dfu_pending) {
      uart_print(".\r\n");
      uart_print("DFU trigger received, rebooting to bootloader\r\n");
      __asm__ volatile ("cpsid i" ::: "memory");
      boot_flags.dfu_requested = DFU_REQUEST;
      boot_flags.reset_reason = RESET_REASON_SOFTWARE;
      boot_flags.magic = BOOT_FLAGS_MAGIC;
      enter_bootloader();
    }
    task_delay_ms(10);
  }
}

// LOW (priority 1): holds the mutex and busy-spins, printing its live priority
// every PRINT_MS ms so the boost from HIGH is visible in the output.
void low_task(void *params)
{
  (void)params;
  task_delay_ms(2000);
  uint32_t counter = 0;


  while (1) {
    for (int i = 0; i < 1000; i++) {
      uart_printf("[%5u] LOW\r\n",
                  i);
    }


    // uint64_t start = scheduler_get_tick_count();
    // uint64_t deadline = start + HOLD_MS;
    // uint64_t next_print = start + PRINT_MS;

    // while (scheduler_get_tick_count() < deadline) {
    // if (scheduler_get_tick_count() >= next_print) {
    // counter++;
    // uart_printf("[%5u] LOW\r\n",
    // counter);
    // next_print += PRINT_MS;
    // }
    // }

    // counter = 0;

    task_delay_ms(10U);
  }
}

// MID (priority 2): prints a tick every 50 ms and never touches the mutex.
// These prints go silent during the window where HIGH is blocked and LOW is
// boosted above MID.
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

// HIGH (priority 3): sleeps 100 ms so LOW can acquire the mutex first, then
// contends on it.  The moment HIGH blocks, mutex_lock boosts LOW to priority 3.
void high_task(void *params)
{
  (void)params;
  uint32_t round = 0;

  while (1) {
    round++;
    uart_printf("[%5u] HIGH: [round %u] blocking on mutex"
                " — LOW should be boosted to priority 3 now\r\n",
                (uint32_t)scheduler_get_tick_count(), round);

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

  scheduler_init(&clk_freq, hz, &tick_count);
  uart_task_start();

  task_create(low_task, 2048, TASK_PRIORITY_1, NULL, &tcb_low);
  // task_create(mid_task, 2048, TASK_PRIORITY_2, NULL, &tcb_mid);
  // task_create(high_task, 2048, TASK_PRIORITY_3, NULL, &tcb_high);
  task_create(dfu_trigger_task, 1024, TASK_PRIORITY_4, NULL, &tcb_dfu);

  // watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 2);
  // watchdog_task_start();

  gic_init();
  gentimer_init(&clk_freq, hz);
  dfu_trigger_reset();
  __asm__ volatile ("cpsie i" ::: "memory");

  schedulerStart();
}
