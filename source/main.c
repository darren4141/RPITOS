#include "boot_flags.h"
#include "delay.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "jtag.h"
#include "reset.h"
#include "scheduler.h"
#include "task.h"
#include "uart.h"

#include <stdbool.h>
#include <stdint.h>

static volatile bool gpio_state = true;

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static uint32_t hz = 1000;

static TaskControlBlock *tcb_1 = NULL;
static TaskControlBlock *tcb_2 = NULL;
static TaskControlBlock *tcb_3 = NULL;
static TaskControlBlock *tcb_4 = NULL;
static TaskControlBlock *tcb_5 = NULL;
static TaskControlBlock *tcb_dfu_trigger = NULL;

// Watches UART RX for the host's raw DFU trigger key. On match, sets the
// DFU flag and reboots into the bootloader — no ack from here, only the
// bootloader acks once it's actually ready to receive.
void dfu_trigger_task(void *params)
{
  while (1) {
    // uart_printf("dfu trigger: %d\r\n", dfu_trigger_get_val());
    if (dfu_pending) {
      uart_print("DFU trigger received, rebooting to bootloader\r\n");
      __asm__ volatile ("cpsid i" ::: "memory");
      boot_flags.dfu_requested = DFU_REQUEST;
      boot_flags.reset_reason = RESET_REASON_SOFTWARE;
      enter_bootloader();
    }
    task_delay_ms(10);
  }
}

void task_1_func(void *params)
{
  uint64_t last_wake_time = tick_count;
  uart_print("Task 1 starting.........\r\n");
  uint32_t count_1 = 0;
  while (1) {
    count_1++;
    uart_printf("(%d) Task 1 | %d | %d | %d |\r\n", count_1, (uint32_t)tcb_1->p_Stack, (uint32_t)tcb_1->p_TopOfStack, (uint32_t)tcb_1->p_EndOfStack);
    task_delay_until_ms(&last_wake_time, 500);
  }
}

void task_2_func(void *params)
{
  uart_print("Task 2 starting.........\r\n");
  uint32_t count_2 = 0;
  while (1) {
    count_2++;
    uart_printf("(%d) Task 2 | %d | %d | %d |\r\n", count_2, (uint32_t)tcb_2->p_Stack, (uint32_t)tcb_2->p_TopOfStack, (uint32_t)tcb_2->p_EndOfStack);
    task_delay_ms(1000);
  }
}

void task_3_func(void *params)
{
  uart_print("Task 3 starting.........\r\n");
  uint32_t count_3 = 0;
  while (1) {
    count_3++;
    uart_printf("(%d) Task 3 | %d | %d | %d |\r\n", count_3, (uint32_t)tcb_3->p_Stack, (uint32_t)tcb_3->p_TopOfStack, (uint32_t)tcb_3->p_EndOfStack);
    task_delay_ms(100);
  }
}

void task_4_func(void *params)
{
  uart_print("Task 4 starting.........\r\n");
  uint32_t count_4 = 0;
  while (1) {
    count_4++;
    uart_printf("(%d) Task 4 | %d | %d | %d |\r\n", count_4, (uint32_t)tcb_4->p_Stack, (uint32_t)tcb_4->p_TopOfStack, (uint32_t)tcb_4->p_EndOfStack);
    task_delay_ms(250);
  }
}

void task_5_func(void *params)
{
  uart_print("Task 5 starting.........\r\n");
  uint32_t count_5 = 0;
  while (1) {
    count_5++;
    uart_printf("(%d) Task 5 | %d | %d | %d |\r\n", count_5, (uint32_t)tcb_5->p_Stack, (uint32_t)tcb_5->p_TopOfStack, (uint32_t)tcb_5->p_EndOfStack);
    task_delay_ms(200);
  }
}

void kmain(void)
{
  gpio_set_function(16, GPIO_FUNC_OUTPUT);
  jtag_gpio_init();

  uart_init(UART_BAUDRATE_115200);
  uart_print("uart initialized!\r\n");

  scheduler_init(&clk_freq, hz, &tick_count);

  uart_task_start();

  uart_print("Starting main...\r\n");

  uart_print("Creating tasks...\r\n");
  task_create(task_1_func, 512, TASK_PRIORITY_5, NULL, &tcb_1);
  task_create(task_2_func, 512, TASK_PRIORITY_3, NULL, &tcb_2);
  task_create(task_3_func, 512, TASK_PRIORITY_4, NULL, &tcb_3);
  task_create(task_4_func, 512, TASK_PRIORITY_5, NULL, &tcb_4);
  task_create(task_5_func, 512, TASK_PRIORITY_3, NULL, &tcb_5);
  task_create(dfu_trigger_task, 512, TASK_PRIORITY_5, NULL, &tcb_dfu_trigger);

  uart_print("Initializing GIC...\r\n");
  gic_init();

  uart_print("Initializing General Timer...\r\n");
  gentimer_init(&clk_freq, hz);
  delay_init(&tick_count);
  dfu_trigger_reset();
  __asm__ volatile ("cpsie i" ::: "memory");

  schedulerStart();
}
