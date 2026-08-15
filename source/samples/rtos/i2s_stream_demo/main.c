// See README.md for what this sample demonstrates.

#include "boot_flags.h"
#include "cprman.h"
#include "dfu_trigger.h"
#include "gentimer.h"
#include "gic.h"
#include "gpio.h"
#include "i2s.h"
#include "jtag.h"
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

static TaskControlBlock *tcb_i2s_stream = NULL;

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

static I2sConfig i2s_config = {
  .clk_pin = I2S_PIN_CLK,
  .fs_pin = I2S_PIN_FS,
  .din_pin = I2S_PIN_DIN,
  .dout_pin = I2S_PIN_DOUT,
  .alt_func = GPIO_FUNC_ALT0,
  .sample_rate = I2S_SAMPLE_RATE_48000,
};

static WatchdogConfig watchdog_config = {
  .timeout_s = 5,
  .policy = WATCHDOG_RESET_POLICY_FORCE_UPDATE,
  .tolerance = 2,
  .confirm_delay_ms = 1000,
};

// Set by kmain()'s i2s_init() call, checked by i2s_stream_task() before it
// starts spinning the transfer loop — see docs.md's note on why init runs in
// kmain() rather than the task itself.
static StatusCode s_i2s_init_result = E_NOT_INITIALIZED;

#define TONE_HZ         440U
#define STREAM_FRAMES   256U   // stereo frames per i2s_transfer() call
#define TONE_AMPLITUDE  8000   // int16 headroom under full scale (32767)
#define STATUS_PERIOD   100U   // iterations between UART status lines

// MAX98357A SD_MODE — not part of the I2S protocol or the PCM peripheral,
// so this stays app-level rather than in i2s.c (same reasoning i2c.c/
// pwm_pca9685.c are kept separate — a bus driver shouldn't know about a
// specific downstream device). < 0.16V = shutdown (muted); driven high here
// only after i2s_init() confirms PCM_CLK/PCM_FS are running, to avoid the
// amp coming out of shutdown while its clock/frame inputs are absent.
#define AMP_SD_PIN      16U

static int16_t tx_buf[STREAM_FRAMES * 2];   // interleaved L, R
static int16_t rx_buf[STREAM_FRAMES * 2];

// Fills tx_buf with one continuous square wave, phase-continuous across
// calls via a static sample counter. Not a hi-fi tone (integer period
// rounding puts the real frequency a few Hz off 440) — just cheap,
// audible/scope-visible proof the transfer loop is alive.
static void tone_fill(uint32_t sample_rate_hz)
{
  static uint32_t phase = 0;
  uint32_t half_period = sample_rate_hz / (2U * TONE_HZ);

  for (uint32_t frame = 0; frame < STREAM_FRAMES; frame++) {
    int16_t sample = (((phase / half_period) % 2U) == 0U) ? TONE_AMPLITUDE : -TONE_AMPLITUDE;
    tx_buf[frame * 2U]      = sample;   // left
    tx_buf[frame * 2U + 1U] = sample;   // right
    phase++;
  }
}

static void i2s_stream_task(void *params)
{
  (void)params;

  if (s_i2s_init_result != E_OK) {
    uart_printf("i2s_stream: i2s_init failed (%d) — task parked\r\n", s_i2s_init_result);
    while (1) {
      task_delay_ms(1000U);
    }
  }

  uart_print("i2s_stream: streaming ~440 Hz square wave, full-duplex, forever\r\n");

  uint32_t iterations = 0;
  uint32_t timeouts = 0;
  uint64_t start_tick_ms = tick_count;   // hz == 1000, so tick_count is already in ms

  while (1) {
    tone_fill((uint32_t)i2s_config.sample_rate);

    StatusCode xfer = i2s_transfer(tx_buf, rx_buf, STREAM_FRAMES * 2U);
    iterations++;

    // The key diagnostic: i2s_transfer()'s poll loop only times out if
    // CS_A.TXD/RXD never assert, i.e. PCM_CLK isn't actually toggling —
    // surfaced immediately rather than waiting for the next status line.
    if (xfer == E_TIMED_OUT) {
      timeouts++;
      uart_printf("i2s_stream: transfer TIMED_OUT (#%u total, iter %u) — PCM_CLK may not be running\r\n",
                  timeouts, iterations);
    }
    else if (xfer != E_OK) {
      uart_printf("i2s_stream: transfer failed (%d) at iter %u\r\n", xfer, iterations);
    }

    if ((iterations % STATUS_PERIOD) == 0U) {
      uint32_t tx_total, rx_total;
      i2s_transfer_totals(&tx_total, &rx_total);

      // tx_total/rx_total climbing every status line is one liveness
      // signal, but on its own doesn't confirm PCM_CLK is running at the
      // *right* rate — zero timeouts just means every transfer eventually
      // finished within a million idle polls, which would hold even at the
      // wrong clock rate. achieved_frame_hz is the real check: elapsed
      // wall-clock time (scheduler tick, 1 ms resolution) versus frames
      // actually moved (tx_total/2, since tx_total counts interleaved L+R
      // samples) — compare against the configured sample_rate below.
      uint64_t elapsed_ms = tick_count - start_tick_ms;
      uint32_t achieved_frame_hz = (elapsed_ms > 0U)
        ? (uint32_t)(((uint64_t)tx_total / 2ULL) * 1000ULL / elapsed_ms)
        : 0U;

      // CS_A is the raw register (decode against the CS_A_* bitmasks in
      // i2s.h) for anything the other fields don't explain. errors is
      // i2s_error_total() — CS_A.TXERR/RXERR occurrences since i2s_init(),
      // cleared once per i2s_transfer() call after being observed (see
      // i2s/docs.md) — a nonzero and climbing count here means real FIFO
      // access errors, not just a stale one-time latch.
      uart_printf("i2s_stream: iter=%u timeouts=%u errors=%u tx_total=%u rx_total=%u achieved=%uHz(target=%u) rx[0]=%d rx[1]=%d CS_A=0x%08X\r\n",
                  iterations, timeouts, i2s_error_total(), tx_total, rx_total, achieved_frame_hz,
                  (uint32_t)i2s_config.sample_rate, rx_buf[0], rx_buf[1], i2s_last_status());
    }
  }
}

void kmain(void)
{
  jtag_gpio_init();

  uart_init(&uart_config);

  // No task-context requirement here (unlike pwm_pca9685_init() — see
  // pwm_fade_demo/main.c), so this can run directly in kmain(), same as
  // i2c_channel_init() does there.
  s_i2s_init_result = i2s_init(&i2s_config);

  // MAX98357A amp bring-up: release shutdown only after PCM_CLK/PCM_FS are
  // confirmed running (i2s_init() leaves CS_A.TXON/RXON enabled by the time
  // it returns E_OK) — see AMP_SD_PIN's comment above. A failed i2s_init()
  // leaves the amp held in shutdown instead of live with no real clock.
  gpio_set_function(AMP_SD_PIN, GPIO_FUNC_OUTPUT);
  if (s_i2s_init_result == E_OK) {
    gpio_on(AMP_SD_PIN);
  }
  else {
    gpio_off(AMP_SD_PIN);
  }

  uart_print("\r\n=== i2s_stream_demo ===\r\n"
             "Bring-up/liveness check for i2s.c + cprman.c on real hardware — not a\r\n"
             "hi-fi demo. Continuously streams a square wave via the blocking\r\n"
             "i2s_transfer() API and reports E_TIMED_OUT immediately if PCM_CLK never\r\n"
             "starts. Optionally bridge PCM_DOUT (GPIO21) to PCM_DIN (GPIO20) with a\r\n"
             "jumper to see the tone echoed back in the periodic rx[] status line.\r\n"
             "GPIO16 drives a MAX98357A's SD_MODE pin (high = enabled) once i2s_init()\r\n"
             "confirms the clock is running — see README.md before wiring one up.\r\n\r\n");

  if (s_i2s_init_result != E_OK) {
    uart_printf("i2s_stream: i2s_init failed (%d)\r\n", s_i2s_init_result);
  }
  else {
    // Debug aid: confirms what's actually latched into the PCM clock
    // generator's hardware registers, ruling out "the divisor write didn't
    // take" as an explanation if the achieved sample rate ever looks wrong
    // in the periodic status line. Does NOT confirm PCM_CLK is really
    // toggling at this rate on the pin — only a scope/logic analyzer can.
    uint32_t ctl, div;
    cprman_pcm_clock_status(&ctl, &div);
    uint32_t divi = (div >> CM_DIV_FRAC_BITS) & CM_DIV_DIVI_MASK;
    uint32_t divf = div & CM_DIV_DIVF_MASK;
    uart_printf("i2s_stream: PCM clock CTL=0x%08X DIV=0x%08X (DIVI=%u DIVF=%u, BUSY=%u)\r\n",
                ctl, div, divi, divf, (ctl & CM_CTL_BUSY) ? 1U : 0U);
  }

  scheduler_init(0, &clk_freq, hz, &tick_count);
  uart_task_start();

  task_create(i2s_stream_task, 2048, TASK_PRIORITY_1, NULL, "i2s_stream", &tcb_i2s_stream);

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
