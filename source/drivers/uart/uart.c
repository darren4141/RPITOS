#include "uart.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef UART_MINIMAL
#include "dma.h"
#include "gic.h"
#include "interrupts.h"
#include "semaphore.h"
#include "spinlock.h"
#include "task.h"
#include "task_types.h"
#endif

// ── Channel table ─────────────────────────────────────────────────────────────
// SoC-fixed facts, never caller-configurable — see docs.md's "Channel table".

typedef struct {
  PL011Regs *regs;
  uint32_t irq_intid;   // 0 = not wired up / unknown — see docs.md
} UartHwDescriptor;

#define UART_NUM_CHANNELS 6

static const UartHwDescriptor uart_hw_table[UART_NUM_CHANNELS] = {
  [0] = { .regs = (PL011Regs *)UART0_BASE, .irq_intid = UART_IRQ_INTID },
  [1] = { 0 },                                                 // mini-UART — different register layout, unsupported
  [2] = { .regs = (PL011Regs *)UART2_BASE, .irq_intid = 0 },
  [3] = { .regs = (PL011Regs *)UART3_BASE, .irq_intid = 0 },
  [4] = { .regs = (PL011Regs *)UART4_BASE, .irq_intid = 0 },
  [5] = { .regs = (PL011Regs *)UART5_BASE, .irq_intid = 0 },   // UART_CHANNEL_TELEMETRY — TX pin (GPIO12/ALT4) verified, irq_intid unused (TX-only, blocking)
};

// Set by uart_channel_init(); NULL means that channel isn't configured yet.
static UartConfig *s_uart_configs[UART_NUM_CHANNELS] = { NULL };

// ── Hardware init (shared across every channel) ──────────────────────────────

static StatusCode uart_baud_divisors(UartBaudrate baudrate, uint32_t *ibrd, uint32_t *fbrd)
{
  switch (baudrate) {
  case UART_BAUDRATE_115200:
    *ibrd = UART_IBRD_115200;
    *fbrd = UART_FBRD_115200;
    return E_OK;

  case UART_BAUDRATE_921600:
    *ibrd = UART_IBRD_921600;
    *fbrd = UART_FBRD_921600;
    return E_OK;

  default:
    return E_INVALID_ARGS;
  }
}

#ifndef UART_MINIMAL
static uint8_t s_buffered_channel = UART_NUM_CHANNELS;   // sentinel: none yet
static bool uart_task_started = false;
#endif

// Shared bounds/support check + regs lookup used by every per-channel entry point.
static PL011Regs *uart_channel_regs(uint8_t channel)
{
  if ((channel >= UART_NUM_CHANNELS) || (uart_hw_table[channel].regs == NULL)) {
    return NULL;
  }
  return uart_hw_table[channel].regs;
}

StatusCode uart_channel_init(uint8_t channel, UartConfig *config)
{
  if (config == NULL) {
    return E_INVALID_ARGS;
  }
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return E_NOTSUPP;
  }

  gpio_set_function(config->tx_pin, config->alt_func);
  gpio_set_pull(config->tx_pin, GPIO_PULL_NONE);
  if (config->rx_pin != UART_PIN_NONE) {
    gpio_set_function(config->rx_pin, config->alt_func);
    gpio_set_pull(config->rx_pin, GPIO_PULL_NONE);
  }

  regs->CR &= ~1U;

  uint32_t timeout = 10000;
  while ((regs->FR & FR_BUSY) && timeout > 0) {
    timeout--;
  }
  if (timeout == 0) {
    return E_TIMED_OUT;
  }

  regs->LCRH &= ~LCRH_FEN;
  regs->ICR = ICR_ALL;

  uint32_t ibrd, fbrd;
  StatusCode ret = uart_baud_divisors(config->baudrate, &ibrd, &fbrd);
  if (ret != E_OK) {
    return ret;
  }
  regs->IBRD = ibrd;
  regs->FBRD = fbrd;

  regs->LCRH = LCRH_WLEN_8 | LCRH_FEN;
  regs->CR = CR_UARTEN | CR_TXE | ((config->rx_pin != UART_PIN_NONE) ? CR_RXE : 0U);

#ifndef UART_MINIMAL
  if (channel == s_buffered_channel) {
    uart_task_started = false;
  }
#endif

  s_uart_configs[channel] = config;
  return E_OK;
}

StatusCode uart_init(UartConfig *config)
{
  return uart_channel_init(UART_CHANNEL_PRINT, config);
}

void uart_channel_tx_raw(uint8_t channel, uint8_t byte)
{
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return;
  }
  while (regs->FR & FR_TXFF) {}
  regs->DR = byte;
}

void uart_tx_raw(uint8_t byte)
{
  uart_channel_tx_raw(UART_CHANNEL_PRINT, byte);
}

uint8_t uart_channel_rx(uint8_t channel)
{
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return 0;
  }
  while (regs->FR & FR_RXFE) {}
  return (uint8_t)(regs->DR & 0xFF);
}

uint8_t uart_rx()
{
  return uart_channel_rx(UART_CHANNEL_PRINT);
}

StatusCode uart_channel_rx_nonblocking(uint8_t channel, uint8_t *out)
{
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return E_NOTSUPP;
  }
  if (regs->FR & FR_RXFE) {
    return E_EMPTY;
  }
  *out = (uint8_t)(regs->DR & 0xFF);
  return E_OK;
}

StatusCode uart_rx_nonblocking(uint8_t *out)
{
  return uart_channel_rx_nonblocking(UART_CHANNEL_PRINT, out);
}

StatusCode uart_channel_rx_timed(uint8_t channel, uint8_t *out, uint32_t timeout_ms)
{
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return E_NOTSUPP;
  }
  uint32_t frq, lo, hi;
  asm volatile ("mrc  p15, 0, %0, c14, c0, 0" : "=r" (frq));
  asm volatile ("mrrc p15, 0, %0, %1,  c14"   : "=r" (lo), "=r" (hi));
  uint64_t start = ((uint64_t)hi << 32) | lo;
  uint64_t ticks = (uint64_t)(frq / 1000U) * timeout_ms;
  while (regs->FR & FR_RXFE) {
    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
    if ((((uint64_t)hi << 32) | lo) - start >= ticks) {
      return E_TIMED_OUT;
    }
  }
  *out = (uint8_t)(regs->DR & 0xFF);
  return E_OK;
}

StatusCode uart_rx_timed(uint8_t *out, uint32_t timeout_ms)
{
  return uart_channel_rx_timed(UART_CHANNEL_PRINT, out, timeout_ms);
}

void uart_channel_drain(uint8_t channel)
{
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return;
  }
  while (regs->FR & FR_BUSY) {}
}

void uart_drain(void)
{
  uart_channel_drain(UART_CHANNEL_PRINT);
}

void uart_channel_deinit(uint8_t channel)
{
  PL011Regs *regs = uart_channel_regs(channel);
  if (regs == NULL) {
    return;
  }

  uart_channel_drain(channel);   // wait for TX FIFO before touching control registers

  regs->CR &= ~(CR_UARTEN | CR_TXE | CR_RXE);
  regs->LCRH &= ~LCRH_FEN;
  regs->IMSC = 0;
  regs->ICR = ICR_ALL;

  UartConfig *config = s_uart_configs[channel];
  if (config != NULL) {
    gpio_set_function(config->tx_pin, GPIO_FUNC_INPUT);
    if (config->rx_pin != UART_PIN_NONE) {
      gpio_set_function(config->rx_pin, GPIO_FUNC_INPUT);
    }
  }
}

void uart_deinit()
{
  uart_channel_deinit(UART_CHANNEL_PRINT);
}

// ── Full mode: ring-buffer TX + scheduler task ────────────────────────────────
#ifndef UART_MINIMAL

static Semaphore uart_data_ready;              // producers → task: bytes queued
static Semaphore uart_dma_done;                // DMA IRQ → task: transfer complete
// One byte per 32-bit slot. The legacy DMA has no byte-width transfer mode, and
// a 32-bit write to the PL011 DR only latches bits [7:0] — so a packed byte
// buffer would transmit only every 4th byte. Packing one byte per word makes
// each DREQ-paced word-write emit exactly one byte.
static volatile uint32_t uart_buf[UART_BUFFER_SIZE];
static volatile uint16_t p_uart_buf_left = 0;   // index of last byte consumed
static volatile uint16_t p_uart_buf_right = 0;  // index of last byte produced

// Guards the reserve-a-slot-and-write step in uart_channel_tx(): enter_critical()
// alone only stops same-core preemption, so a second core calling
// uart_channel_send_byte()/uart_channel_print() concurrently would race
// p_uart_buf_right/uart_buf[] without this. .bss-zeroed initial state is
// already the unlocked state, so no explicit init call is needed — see
// heap_lock in heap.c for the same pattern.
static Spinlock uart_buf_lock;

static TaskControlBlock *uart_tcb = NULL;

static DmaControlBlock uart_tx_cb __attribute__((aligned(32)));

static void uart_dma_tx_run(uint8_t channel, const volatile uint32_t *buf, uint16_t len)
{
  UartConfig *config = s_uart_configs[channel];
  uart_tx_cb.ti = DMA_TI_INTEN | DMA_TI_WAIT_RESP | DMA_TI_DEST_DREQ
                  | DMA_TI_SRC_INC | DMA_TI_PERMAP(DMA_DREQ_UART_TX);
  uart_tx_cb.source_ad = BUS_ADDRESS(buf);
  uart_tx_cb.dest_ad = PERIPHERAL_BUS_ADDRESS(&uart_hw_table[channel].regs->DR);
  uart_tx_cb.txfr_len = (uint32_t)len * 4U;   // 32-bit beats: one word = one byte
  uart_tx_cb.stride = 0;
  uart_tx_cb.nextconbk = 0;
  dma_start(config->dma_channel, &uart_tx_cb);
}

// Called from _irq_handler when the buffered channel's TX DMA channel raises
// its completion IRQ.
void uart_dma_irq_handler(void)
{
  UartConfig *config = s_uart_configs[s_buffered_channel];
  volatile DmaChannelRegs *ch = DMA_CHANNEL(config->dma_channel);
  ch->CS = DMA_CS_INT;   // write-1-to-clear the channel interrupt latch
  __asm__ volatile ("dsb sy" ::: "memory");
  semaphore_give(&uart_dma_done);
}

// ── RX interrupt (replaces polling RX from the scheduler tick) ────────────────
// RX interrupts are only ever enabled for s_buffered_channel (the single
// channel that owns the shared buffered backend) — in practice that's the
// print channel today, but nothing here hardcodes that.

// DFU-trigger watch hook, fed unconditionally from uart_rx_irq_handler() below
// for every byte, on every app — see the call site for why. dfu_trigger.c
// provides the strong definition; samples that do not link dfu_trigger.o fall
// back to this weak no-op, so uart.c stays resolvable without forcing every
// sample to pull it in.
void dfu_trigger_feed_isr(uint8_t byte);
__attribute__((weak)) void dfu_trigger_feed_isr(uint8_t byte)
{
  (void)byte;
}

static UartRxHandler uart_rx_handler = NULL;

void uart_rx_irq_enable(UartRxHandler handler)
{
  uart_rx_handler = handler;
}

// Called from _irq_handler on the PL011 combined interrupt (RX path only —
// TX goes through DMA, so only RXIM/RTIM are unmasked). RX is only ever
// enabled for the single channel that owns the shared buffered backend
// (s_buffered_channel) — see uart_channel_task_start().
void uart_rx_irq_handler(void)
{
  PL011Regs *regs = uart_hw_table[s_buffered_channel].regs;
  while (!(regs->FR & FR_RXFE)) {
    uint8_t b = (uint8_t)(regs->DR & 0xFF);
    dfu_trigger_feed_isr(b);   // always watch for the DFU recovery key
    if (uart_rx_handler) {
      uart_rx_handler(b);
    }
  }
  regs->ICR = ICR_RXIC | ICR_RTIC;   // clear the RX + timeout latches
}

static void uart_tx_task(void *params)
{
  (void)params;
  uint8_t channel = s_buffered_channel;
  UartConfig *config = s_uart_configs[channel];

  while (1) {
    semaphore_take(&uart_data_ready, SEMAPHORE_TAKE_BLOCKING);

    if (config->is_dma_enabled) {
      while (p_uart_buf_right != p_uart_buf_left) {
        uint16_t left = p_uart_buf_left;
        uint16_t start = (left + 1) % UART_BUFFER_SIZE;
        uint16_t avail = (uint16_t)((p_uart_buf_right - left + UART_BUFFER_SIZE) % UART_BUFFER_SIZE);
        uint16_t run = avail;
        if ((uint32_t)start + run > UART_BUFFER_SIZE) {
          run = (uint16_t)(UART_BUFFER_SIZE - start);
        }

        uart_dma_tx_run(channel, &uart_buf[start], run);
        semaphore_take(&uart_dma_done, SEMAPHORE_TAKE_BLOCKING);

        p_uart_buf_left = (uint16_t)((left + run) % UART_BUFFER_SIZE);
      }
    }
    else {
      // Baseline: classic byte-by-byte PIO drain (spins on FR_TXFF per byte).
      while (p_uart_buf_right != p_uart_buf_left) {
        p_uart_buf_left = (uint16_t)((p_uart_buf_left + 1) % UART_BUFFER_SIZE);
        uart_channel_tx_raw(channel, (uint8_t)uart_buf[p_uart_buf_left]);
      }
    }
  }
}

StatusCode uart_channel_task_start(uint8_t channel)
{
  if ((channel >= UART_NUM_CHANNELS) || (s_uart_configs[channel] == NULL)) {
    return E_NOT_INITIALIZED;
  }

  UartConfig *config = s_uart_configs[channel];
  if (config->mode != UART_MODE_BUFFERED_TASK) {
    return E_INVALID_ARGS;
  }
  if (uart_task_started && (s_buffered_channel != channel)) {
    return E_RESOURCE_EXHAUSTED; // another channel already owns the shared buffered backend
  }
  if ((config->rx_pin != UART_PIN_NONE) && (uart_hw_table[channel].irq_intid == 0)) {
    return E_NOTSUPP;            // this channel's combined IRQ isn't wired up yet — see docs.md
  }

  // Must be set before DMA/RX interrupts are enabled at the GIC below —
  // uart_dma_irq_handler()/uart_rx_irq_handler() both resolve their channel
  // via s_buffered_channel, so an interrupt arriving before this line would
  // read the stale value (sentinel UART_NUM_CHANNELS on first start — an
  // out-of-bounds uart_hw_table[] access).
  s_buffered_channel = channel;

  semaphore_init(&uart_data_ready, 1, 0, "uart_data_ready");

  if (config->is_dma_enabled) {
    semaphore_init(&uart_dma_done, 1, 0, "uart_dma_done");
    uart_hw_table[channel].regs->DMACR = DMACR_TXDMAE;   // gate TX DREQ to the DMA
    dma_channel_init(config->dma_channel);
    irq_register(DMA_IRQ_INTID(config->dma_channel), uart_dma_irq_handler);
    gic_enable_spi(DMA_IRQ_INTID(config->dma_channel), 0x80);
  }

  // RX interrupts are always enabled for a channel with an rx_pin, independent
  // of whether the app ever calls uart_rx_irq_enable() — this guarantees
  // uart_rx_irq_handler() (and therefore the built-in DFU-trigger watch inside
  // it) always runs, so an app can't accidentally ship without DFU recovery
  // support. RX FIFO threshold stays at reset default (1/8); RTIM catches the
  // tail so a short burst (e.g. the 4-byte DFU key) is delivered without
  // waiting to fill.
  if (config->rx_pin != UART_PIN_NONE) {
    uart_hw_table[channel].regs->ICR = ICR_ALL;                  // clear any stale latched interrupts
    uart_hw_table[channel].regs->IMSC |= IMSC_RXIM | IMSC_RTIM;  // enable RX-level + RX-timeout
    irq_register(uart_hw_table[channel].irq_intid, uart_rx_irq_handler);
    gic_enable_spi(uart_hw_table[channel].irq_intid, 0x80);
  }

  StatusCode ret = task_create(uart_tx_task, config->task_stack_words,
                               (TaskPriorityLevel)config->task_priority, NULL, "uart_tx", &uart_tcb);
  if (ret == E_OK) {
    uart_task_started = true;
  }
  return ret;
}

StatusCode uart_task_start(void)
{
  return uart_channel_task_start(UART_CHANNEL_PRINT);
}

static void uart_channel_tx(uint8_t channel, uint8_t byte)
{
  if (!uart_task_started || (channel != s_buffered_channel)) {
    uart_channel_tx_raw(channel, byte);
    return;
  }
  uint16_t next = (p_uart_buf_right + 1) % UART_BUFFER_SIZE;
  if (next == p_uart_buf_left) {
    return;   // buffer full, drop byte
  }
  uart_buf[next] = byte;
  p_uart_buf_right = next;
}

void uart_channel_send_byte(uint8_t channel, uint8_t byte)
{
  if (uart_task_started && (channel == s_buffered_channel)) {
    uint32_t cpsr = enter_critical();
    spinlock_acquire(&uart_buf_lock);
    uart_channel_tx(channel, byte);
    spinlock_release(&uart_buf_lock);
    exit_critical(cpsr);
    semaphore_give(&uart_data_ready);
  }
  else {
    uart_channel_tx_raw(channel, byte);
  }
}

void uart_send_byte(uint8_t byte)
{
  uart_channel_send_byte(UART_CHANNEL_PRINT, byte);
}

void uart_channel_print(uint8_t channel, const char *str)
{
  if (uart_task_started && (channel == s_buffered_channel)) {
    uint32_t cpsr = enter_critical();
    spinlock_acquire(&uart_buf_lock);
    while (*str) {
      uart_channel_tx(channel, (uint8_t)*str++);
    }
    spinlock_release(&uart_buf_lock);
    exit_critical(cpsr);
    semaphore_give(&uart_data_ready);
  }
  else {
    while (*str) {
      uart_channel_tx_raw(channel, (uint8_t)*str++);
    }
  }
}

// ── Minimal mode: blocking TX, no task, no ring buffer ────────────────────────
#else

void uart_channel_print(uint8_t channel, const char *str)
{
  while (*str) {
    uart_channel_tx_raw(channel, (uint8_t)*str++);
  }
}

#endif

void uart_print(const char *str)
{
  uart_channel_print(UART_CHANNEL_PRINT, str);
}

// ── Shared: printf (chunked sink, flushed via uart_channel_print) ────────────

#define PRINTF_CHUNK_SIZE   32
#define PRINTF_NUM_BUF_SIZE 16   // widest real width in this codebase is %08X

// Right-justifies the base-`base` digits of n into out, padded to at least
// `width` characters with `pad`. Pure — no I/O — so it's also safe to call
// from uart_fault_report() in a fault-handler context.
static int format_uint(char *out, uint32_t n, uint32_t base, const char *digits, int width, char pad)
{
  char tmp[10];
  int i = 0;

  if (n == 0) {
    tmp[i++] = '0';
  }
  else {
    while (n > 0) {
      tmp[i++] = digits[n % base];
      n /= base;
    }
  }

  int len = 0;
  for (int p = i; p < width; p++) {
    out[len++] = pad;
  }
  while (i > 0) {
    out[len++] = tmp[--i];
  }
  return len;
}

// Small fixed-size sink that flushes to uart_channel_print() — one critical
// section and one semaphore_give per flush — whenever it fills, instead of
// buffering an entire formatted line on the caller's stack.
typedef struct {
  uint8_t channel;
  char buf[PRINTF_CHUNK_SIZE];
  int pos;
} PrintfSink;

static void sink_flush(PrintfSink *sink)
{
  if (sink->pos > 0) {
    sink->buf[sink->pos] = '\0';
    uart_channel_print(sink->channel, sink->buf);
    sink->pos = 0;
  }
}

static void sink_putc(PrintfSink *sink, char c)
{
  if (sink->pos >= (int)sizeof(sink->buf) - 1) {
    sink_flush(sink);
  }
  sink->buf[sink->pos++] = c;
}

static void sink_puts(PrintfSink *sink, const char *s)
{
  while (*s) {
    sink_putc(sink, *s++);
  }
}

// Formats one padded number (%d/%u/%x/%X) and pushes it through the sink.
static void sink_put_uint(PrintfSink *sink, uint32_t n, uint32_t base, const char *digits, int width, char pad)
{
  char numbuf[PRINTF_NUM_BUF_SIZE];
  if (width > (int)sizeof(numbuf)) {
    width = (int)sizeof(numbuf);
  }

  int len = format_uint(numbuf, n, base, digits, width, pad);
  for (int i = 0; i < len; i++) {
    sink_putc(sink, numbuf[i]);
  }
}

static void uart_vprintf(uint8_t channel, const char *fmt, va_list args)
{
  PrintfSink sink = { .channel = channel, .pos = 0 };

  while (*fmt) {
    if (*fmt != '%') {
      sink_putc(&sink, *fmt++);
      continue;
    }
    fmt++;

    char pad = ' ';
    if (*fmt == '0') {
      pad = '0';
      fmt++;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt++ - '0');
    }

    switch (*fmt) {
    case 'c':
      sink_putc(&sink, (char)va_arg(args, int));
      break;

    case 's': {
      const char *s = va_arg(args, const char *);
      sink_puts(&sink, s ? s : "(null)");
      break;
    }

    case 'd': {
      int32_t n = va_arg(args, int32_t);
      if (n < 0) {
        sink_putc(&sink, '-');
        sink_put_uint(&sink, (uint32_t)-n, 10, "0123456789", (width > 0) ? width - 1 : 0, pad);
      }
      else {
        sink_put_uint(&sink, (uint32_t)n, 10, "0123456789", width, pad);
      }
      break;
    }

    case 'u':
      sink_put_uint(&sink, va_arg(args, uint32_t), 10, "0123456789", width, pad);
      break;

    case 'x':
      sink_put_uint(&sink, va_arg(args, uint32_t), 16, "0123456789abcdef", width, pad);
      break;

    case 'X':
      sink_put_uint(&sink, va_arg(args, uint32_t), 16, "0123456789ABCDEF", width, pad);
      break;

    case '%':
      sink_putc(&sink, '%');
      break;

    default:
      sink_putc(&sink, '%');
      sink_putc(&sink, *fmt);
      break;
    }
    fmt++;
  }

  sink_flush(&sink);
}

void uart_channel_printf(uint8_t channel, const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  uart_vprintf(channel, fmt, args);
  va_end(args);
}

void uart_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  uart_vprintf(UART_CHANNEL_PRINT, fmt, args);
  va_end(args);
}

// Always emits exactly 8 hex digits (no leading-zero trim) via format_uint,
// then writes them out with uart_tx_raw() directly — no ring buffer, no
// critical section, no semaphore — so this stays safe to call from a fault
// handler where scheduler/semaphore state may not be trustworthy.
static void fault_puthex(uint32_t v)
{
  char digits[8];
  format_uint(digits, v, 16, "0123456789ABCDEF", 8, '0');

  uart_tx_raw('0');
  uart_tx_raw('x');
  for (int i = 0; i < 8; i++) {
    uart_tx_raw((uint8_t)digits[i]);
  }
}

void uart_fault_report(uint32_t kind, uint32_t pc, uint32_t addr, uint32_t status)
{
  uart_tx_raw('\r');
  uart_tx_raw('\n');

  uart_tx_raw('P');uart_tx_raw('C');uart_tx_raw('=');
  fault_puthex(pc);

  uart_tx_raw(' ');uart_tx_raw('A');uart_tx_raw('=');
  fault_puthex(addr);

  uart_tx_raw(' ');uart_tx_raw('S');uart_tx_raw('=');
  fault_puthex(status);

  uart_tx_raw(' ');uart_tx_raw('K');uart_tx_raw('=');
  fault_puthex(kind);

  uart_tx_raw('\r');
  uart_tx_raw('\n');
}
