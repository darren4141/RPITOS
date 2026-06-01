#include "delay.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "scheduler.h"
#include "task.h"
#include "uart.h"

#include <stdbool.h>
#include <stdint.h>

static volatile bool gpio_state = true;

static uint32_t clk_freq;
static uint32_t hz = 1000;
static uint32_t tick_count = 0;

static TaskControlBlock *tcb_1 = NULL;
static TaskControlBlock *tcb_2 = NULL;

void task_1_func(void *params)
{
  delay_ms(500);
  uart_print("Task 1 starting.........\r\n");
  uint32_t count_1 = 0;
  while (1) {
    count_1++;
    uart_printf("(%d) Task 1 | %d | %d | %d |\r\n", count_1, (uint32_t)tcb_1->p_Stack, (uint32_t)tcb_1->p_TopOfStack, (uint32_t)tcb_1->p_EndOfStack);
    delay_ms(500);
  }
}

void task_2_func(void *params)
{
  uart_print("Task 2 starting.........\r\n");
  uint32_t count_2 = 0;
  while (1) {
    count_2++;
    uart_printf("(%d) Task 2 | %d | %d | %d |\r\n", count_2, (uint32_t)tcb_2->p_Stack, (uint32_t)tcb_2->p_TopOfStack, (uint32_t)tcb_2->p_EndOfStack);
    delay_ms(1000);
  }
}

void kmain(void)
{
  scheduler_init();

  uart_init(UART_BAUDRATE_115200);

  uart_print("Starting main...\r\n");
  gpio_set_function(16, GPIO_FUNC_OUTPUT);
  gpio_on(16);

  uart_print("Creating tasks...\r\n");
  task_create(task_1_func, 512, TASK_PRIORITY_5, NULL, &tcb_1);
  task_create(task_2_func, 512, TASK_PRIORITY_5, NULL, &tcb_2);

  uart_print("Initializing GIC...\r\n");
  gic_init();

  uart_print("Initializing General Timer...\r\n");
  gentimer_init(&clk_freq, hz);
  delay_init(&clk_freq);
  __asm__ volatile ("cpsie i" ::: "memory");

  schedulerStart();
}

#define BLINK_HZ 2   /* toggle GPIO at 2 Hz → visible 1 Hz blink */

void __attribute__((noinline)) timer_tick_handler(void)
{
  // Read current CVAL and advance by one interval
  uint32_t lo, hi;
  __asm__ volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (lo), "=r" (hi));         // CNTP_CVAL read

  uint64_t cval = ((uint64_t)hi << 32) | lo;
  cval += (clk_freq / hz);                                                      // advance by one interval

  uint32_t new_lo = (uint32_t)(cval & 0xFFFFFFFF);
  uint32_t new_hi = (uint32_t)(cval >> 32);
  __asm__ volatile ("mcrr p15, 2, %0, %1, c14" : : "r" (new_lo), "r" (new_hi)); // CNTP_CVAL write

  tick_count++;
  if (tick_count > 5000) {
    tick_count = 0;
    uart_print("Heartbeat\r\n");
  }
}
