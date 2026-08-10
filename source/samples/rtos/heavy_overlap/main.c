#include "boot_flags.h"
#include "delay.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "mutex.h"
#include "queue.h"
#include "reset.h"
#include "scheduler.h"
#include "semaphore.h"
#include "software_timer.h"
#include "task.h"
#include "uart.h"
#include "watchdog.h"

#include <stdbool.h>
#include <stdint.h>

#define LED_PIN_CORE1 16U   // toggled by the bare loop on core 1

static volatile bool gpio_state = true;

static Queue test_queue;

static Semaphore shared_semaphore;
static volatile uint32_t shared_counter_2 = 0;

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static uint32_t hz = 1000;

static TaskControlBlock *tcb_1 = NULL;
static TaskControlBlock *tcb_2 = NULL;
static TaskControlBlock *tcb_3 = NULL;
static TaskControlBlock *tcb_4 = NULL;
static TaskControlBlock *tcb_5 = NULL;

static StatusCode ret;

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

void task_1_func(void *params)
{
  uart_print("Task 1 starting.........\r\n");
  uint32_t msg = 0;
  while (1) {
    StatusCode ret = queue_send(&test_queue, &msg, 200);
    if (ret == E_OK) {
      msg++;
      uart_printf("Task 1 | sent %u\r\n", msg);
    }
    else {
      uart_printf("Task 1 | queue full, send timed out\r\n");
    }
    task_delay_ms(100);
  }
}

void task_4_func(void *params)
{
  uart_print("Task 4 starting.........\r\n");
  uint32_t recv_count = 0;
  uint32_t msg;

  while (1) {
    task_delay_ms(1000);
    uart_print("Task 4 | draining queue...\r\n");
    while (queue_recv(&test_queue, &msg, 0) == E_OK) {
      recv_count++;
      uart_printf("Task 4 | recv [%u] msg = %u\r\n", recv_count, msg);
    }
    uart_print("Task 4 | queue empty\r\n");
  }
}

void task_2_func(void *params)
{
  uart_print("Task 2 starting.........\r\n");
  while (1) {
    task_delay_ms(1000);
    semaphore_give(&shared_semaphore);
    uart_print("Task 2 | giving semaphore\r\n");
  }
}

void task_3_func(void *params)
{
  uart_print("Task 3 starting.........\r\n");
  StatusCode ret;
  while (1) {
    ret = semaphore_take(&shared_semaphore, 100);

    if (ret == E_OK) {
      shared_counter_2++;
      uart_printf("Task 3 | semaphore taken - %u\r\n", shared_counter_2);
    }
    else {
      uart_print("Task 3 | semaphore timed out\r\n");
    }

    task_delay_ms(100);
  }
}

void task_5_func(void *params)
{
  uart_print("Task 5 starting.........\r\n");
  uint32_t count_5 = 0;
  while (1) {
    count_5++;
    uart_printf("(%d) Task 5 | %d | %d | %d |\r\n", count_5, (uint32_t)tcb_5->stack_base, (uint32_t)tcb_5->current_sp, (uint32_t)tcb_5->stack_high);
    task_delay_ms(200);
  }
}

void kmain(void)
{
  gpio_set_function(16, GPIO_FUNC_OUTPUT);
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_print("uart initialized!\r\n");

  scheduler_init(0, &clk_freq, hz, &tick_count);

  uart_task_start();

  uart_print("Starting main...\r\n");

  uart_print("Creating tasks...\r\n");


  ret = task_create(task_1_func, 2048, TASK_PRIORITY_5, NULL, "task_1", &tcb_1);
  if (ret != E_OK) {
    uart_printf("Create task 1 failed with exit code %d\r\n", ret);
  }

  ret = task_create(task_4_func, 2048, TASK_PRIORITY_5, NULL, "task_4", &tcb_4);
  if (ret != E_OK) {
    uart_printf("Create task 4 failed with exit code %d\r\n", ret);
  }


  ret = task_create(task_2_func, 2048, TASK_PRIORITY_3, NULL, "task_2", &tcb_2);
  if (ret != E_OK) {
    uart_printf("Create task 2 failed with exit code %d\r\n", ret);
  }

  ret = task_create(task_3_func, 2048, TASK_PRIORITY_4, NULL, "task_3", &tcb_3);
  if (ret != E_OK) {
    uart_printf("Create task 3 failed with exit code %d\r\n", ret);
  }


  ret = task_create(task_5_func, 2048, TASK_PRIORITY_1, NULL, "task_5", &tcb_5);
  if (ret != E_OK) {
    uart_printf("Create task 5 failed with exit code %d\r\n", ret);
  }

  software_timer_init();
  software_timer_start();

  watchdog_init(5, WATCHDOG_RESET_POLICY_FORCE_UPDATE, 3, 1000);
  ret = watchdog_task_start();
  if (ret != E_OK) {
    uart_printf("Create watchdog task failed with exit code %d\r\n", ret);
  }

  uart_print("Initializing GIC...\r\n");
  gic_distributor_init();
  gic_percore_init();

  uart_print("Initializing General Timer...\r\n");
  gentimer_init(&clk_freq, hz);
  delay_init(&tick_count);
  queue_init(&test_queue, 4, sizeof(uint32_t), "test_queue");
  semaphore_init(&shared_semaphore, 1, 1, "shared_sem");
  // DFU recovery is wired up automatically now (scheduler_init() + uart_task_start()) —
  // no per-app call needed. See dfu_trigger.h.

  // Release core 1 into its bare blink loop. Safe to call before the scheduler
  // starts — it just publishes the entry and wakes the parked core.
  if (companion_core_start(1U, core1_blink) == E_OK) {
    uart_print("core 0: released core 1\r\n");
  }
  else {
    uart_print("core 0: companion_core_start failed\r\n");
  }

  // A/B trial boot: confirm this app slot now that init succeeded.
  wdt_meta_confirm_slot();

  __asm__ volatile ("cpsie i" ::: "memory");

  scheduler_start();
}
