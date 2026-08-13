// See README.md for what this sample demonstrates.

#include "boot_flags.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "i2c.h"
#include "jtag.h"
#include "pwm_pca9685.h"
#include "reset.h"
#include "scheduler.h"
#include "software_timer.h"
#include "task.h"
#include "uart.h"
#include "watchdog.h"

#include <stdint.h>

static volatile uint32_t clk_freq;
static volatile uint64_t tick_count = 0;
static const uint32_t hz = 1000;   // 1 kHz tick → 1 tick == 1 ms

static TaskControlBlock *tcb_fade = NULL;

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
// per i2c/docs.md's channel table (a phase-1-confirmed channel). Same wiring
// multicore_full_demo uses.
static I2cConfig pca9685_i2c_config = {
  .sda_pin = 4,
  .scl_pin = 5,
  .alt_func = GPIO_FUNC_ALT5,
  .baudrate = I2C_BAUDRATE_STANDARD_100K,
};

static Pca9685Config pca9685_config = {
  .channel = I2C_CHANNEL_3,
  .i2c_addr = PCA9685_I2C_ADDR_DEFAULT,
  .pwm_freq_hz = 200,   // well within PCA9685_FREQ_HZ_MIN..MAX, plenty above visible flicker
};

// Same two PCA9685 output channels multicore_full_demo's pca_blink_task
// full-on/full-off blinks — here driven with an actual duty-cycle ramp
// instead, so the LEDs breathe rather than snap.
#define PCA_FADE_CHANNEL_A 4U
#define PCA_FADE_CHANNEL_B 5U

#define FADE_STEPS         100U // triangle-wave resolution per ramp
#define FADE_STEP_MS       20U  // 100 steps * 20 ms = 2 s per ramp, 4 s full breathe cycle

static WatchdogConfig watchdog_config = {
  .timeout_s = 5,
  .policy = WATCHDOG_RESET_POLICY_FORCE_UPDATE,
  .tolerance = 2,
  .confirm_delay_ms = 1000,
};

// Drives channel A to step/FADE_STEPS of full brightness and channel B to
// the complement, so one brightens while the other dims (crossfade, not two
// independent fades).
static void pca_set_fade_level(uint32_t step)
{
  uint32_t duty_a = (uint32_t)(((uint64_t)step * UINT32_MAX) / FADE_STEPS);
  uint32_t duty_b = UINT32_MAX - duty_a;
  pwm_pca9685_set_channel(PCA_FADE_CHANNEL_A, 0, duty_a);
  pwm_pca9685_set_channel(PCA_FADE_CHANNEL_B, 0, duty_b);
}

// pwm_pca9685_init() itself must run in task context (it blocks on
// task_delay_ms() for the datasheet's post-SLEEP-clear settle time — see
// pwm_pca9685/docs.md), so it happens here at task start rather than in
// kmain(); i2c_channel_init() has no such requirement and already ran in
// kmain() before scheduler_start().
static void pca_fade_task(void *params)
{
  (void)params;

  StatusCode ret = pwm_pca9685_init(&pca9685_config);
  if (ret != E_OK) {
    uart_printf("pwm_fade: pwm_pca9685_init failed (%d) — fade task parked\r\n", ret);
    while (1) {
      task_delay_ms(1000U);
    }
  }

  uart_print("pwm_fade: breathing channels 4 and 5 (crossfade)\r\n");

  while (1) {
    for (uint32_t step = 0; step <= FADE_STEPS; step++) {     // A up, B down
      pca_set_fade_level(step);
      task_delay_ms(FADE_STEP_MS);
    }
    for (uint32_t step = FADE_STEPS; step > 0; step--) {      // A down, B up
      pca_set_fade_level(step - 1U);
      task_delay_ms(FADE_STEP_MS);
    }
  }
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(&uart_config);

  // No task-context requirement here (unlike pwm_pca9685_init() — see
  // pca_fade_task()), so this can run directly in kmain().
  i2c_channel_init(I2C_CHANNEL_3, &pca9685_i2c_config);

  uart_print("\r\n=== pwm_fade_demo ===\r\n"
             "PCA9685 channels 4 and 5 crossfade at 12-bit duty resolution\r\n"
             "instead of the full-on/full-off blink in multicore_full_demo.\r\n\r\n");

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  task_create(pca_fade_task, 2048, TASK_PRIORITY_1, NULL, "pca_fade", &tcb_fade);

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
