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

// ── Hardware init (shared) ────────────────────────────────────────────────────

static StatusCode uart_hw_init(UartBaudrate baudrate)
{
  gpio_set_function(14, GPIO_FUNC_ALT0);
  gpio_set_function(15, GPIO_FUNC_ALT0);
  gpio_set_pull(14, GPIO_PULL_NONE);
  gpio_set_pull(15, GPIO_PULL_NONE);

  uint32_t val = UART0->CR;
  val &= ~1U;
  UART0->CR = val;

  uint32_t timeout = 10000;
  while (UART0->FR & FR_BUSY && timeout > 0) {
    timeout--;
  }
  if (timeout == 0) {
    return E_TIMED_OUT;
  }

  UART0->LCRH &= ~LCRH_FEN;
  UART0->ICR = ICR_ALL;

  switch (baudrate) {
  case UART_BAUDRATE_115200:
    UART0->IBRD = UART_IBRD_115200;
    UART0->FBRD = UART_FBRD_115200;
    break;

  default:
    return E_INVALID_ARGS;
  }

  UART0->LCRH = LCRH_WLEN_8 | LCRH_FEN;
  UART0->CR = CR_UARTEN | CR_TXE | CR_RXE;

  return E_OK;
}

void uart_tx_raw(uint8_t byte)
{
  while (UART0->FR & FR_TXFF) {}
  UART0->DR = byte;
}

uint8_t uart_rx()
{
  while (UART0->FR & FR_RXFE) {}
  return (uint8_t)(UART0->DR & 0xFF);
}

StatusCode uart_rx_nonblocking(uint8_t *out)
{
  if (UART0->FR & FR_RXFE) {
    return E_EMPTY;
  }
  *out = (uint8_t)(UART0->DR & 0xFF);
  return E_OK;
}

StatusCode uart_rx_timed(uint8_t *out, uint32_t timeout_ms)
{
  uint32_t frq, lo, hi;
  asm volatile ("mrc  p15, 0, %0, c14, c0, 0" : "=r" (frq));
  asm volatile ("mrrc p15, 0, %0, %1,  c14"   : "=r" (lo), "=r" (hi));
  uint64_t start = ((uint64_t)hi << 32) | lo;
  uint64_t ticks = (uint64_t)frq * timeout_ms / 1000ULL;
  while (UART0->FR & FR_RXFE) {
    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
    if ((((uint64_t)hi << 32) | lo) - start >= ticks) {
      return E_TIMED_OUT;
    }
  }
  *out = (uint8_t)(UART0->DR & 0xFF);
  return E_OK;
}

void uart_drain(void)
{
  while (UART0->FR & FR_BUSY) {}
}

void uart_deinit()
{
  uart_drain();   // wait for TX FIFO before touching control registers

  UART0->CR &= ~(CR_UARTEN | CR_TXE | CR_RXE);
  UART0->LCRH &= ~LCRH_FEN;
  UART0->IMSC = 0;
  UART0->ICR = ICR_ALL;

  gpio_set_function(14, GPIO_FUNC_INPUT);
  gpio_set_function(15, GPIO_FUNC_INPUT);
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

// Guards the reserve-a-slot-and-write step in uart_tx(): enter_critical() alone
// only stops same-core preemption, so a second core calling uart_send_byte()/
// uart_print() concurrently would race p_uart_buf_right/uart_buf[] without
// this. .bss-zeroed initial state is already the unlocked state, so no
// explicit init call is needed — see heap_lock in heap.c for the same pattern.
static Spinlock uart_buf_lock;

static TaskControlBlock *uart_tcb = NULL;
static bool uart_task_started = false;

#if UART_TX_DMA
static DmaControlBlock uart_tx_cb __attribute__((aligned(32)));

// Launch one DMA transfer of `len` bytes (one word per byte) from the ring
// buffer to UART0->DR, paced by the UART TX DREQ. The span must be contiguous.
static void uart_dma_tx_run(const volatile uint32_t *buf, uint16_t len)
{
  uart_tx_cb.ti = DMA_TI_INTEN | DMA_TI_WAIT_RESP | DMA_TI_DEST_DREQ
                  | DMA_TI_SRC_INC | DMA_TI_PERMAP(DMA_DREQ_UART_TX);
  uart_tx_cb.source_ad = BUS_ADDRESS(buf);
  uart_tx_cb.dest_ad = UART0_DR_BUS;
  uart_tx_cb.txfr_len = (uint32_t)len * 4U;   // 32-bit beats: one word = one byte
  uart_tx_cb.stride = 0;
  uart_tx_cb.nextconbk = 0;
  dma_start(UART_DMA_TX_CHANNEL, &uart_tx_cb);
}
#endif

// Called from _irq_handler when the TX DMA channel raises its completion IRQ.
void uart_dma_irq_handler(void)
{
  volatile DmaChannelRegs *ch = DMA_CHANNEL(UART_DMA_TX_CHANNEL);
  ch->CS = DMA_CS_INT;   // write-1-to-clear the channel interrupt latch
  __asm__ volatile ("dsb sy" ::: "memory");
  semaphore_give(&uart_dma_done);
}

// ── RX interrupt (replaces polling RX from the scheduler tick) ────────────────

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

// Registers an ADDITIONAL app-specific RX handler on top of the built-in DFU
// watch — it does not gate whether RX interrupts are enabled (uart_task_start()
// always enables them) or whether the DFU key is watched for (uart_rx_irq_handler()
// always feeds it). An app that wants no extra handling doesn't need to call this.
void uart_rx_irq_enable(UartRxHandler handler)
{
  uart_rx_handler = handler;
}

// Called from _irq_handler on the PL011 combined interrupt (RX path only —
// TX goes through DMA, so only RXIM/RTIM are unmasked).
void uart_rx_irq_handler(void)
{
  while (!(UART0->FR & FR_RXFE)) {
    uint8_t b = (uint8_t)(UART0->DR & 0xFF);
    dfu_trigger_feed_isr(b);   // always watch for the DFU recovery key
    if (uart_rx_handler) {
      uart_rx_handler(b);
    }
  }
  UART0->ICR = ICR_RXIC | ICR_RTIC;   // clear the RX + timeout latches
}

void uart_tx_task(void *params)
{
  (void)params;
  while (1) {
    semaphore_take(&uart_data_ready, SEMAPHORE_TAKE_BLOCKING);

#if UART_TX_DMA
    // Drain the ring buffer one contiguous run at a time. A run reaches from the
    // first unconsumed byte to either the write head or the end of the array
    // (whichever comes first) — DMA needs a linear span, so wraps split in two.
    while (p_uart_buf_right != p_uart_buf_left) {
      uint16_t left = p_uart_buf_left;
      uint16_t start = (left + 1) % UART_BUFFER_SIZE;
      uint16_t avail = (uint16_t)((p_uart_buf_right - left + UART_BUFFER_SIZE) % UART_BUFFER_SIZE);
      uint16_t run = avail;
      if ((uint32_t)start + run > UART_BUFFER_SIZE) {
        run = (uint16_t)(UART_BUFFER_SIZE - start);
      }

      uart_dma_tx_run(&uart_buf[start], run);
      semaphore_take(&uart_dma_done, SEMAPHORE_TAKE_BLOCKING);

      p_uart_buf_left = (uint16_t)((left + run) % UART_BUFFER_SIZE);
    }
#else
    // Baseline: classic byte-by-byte PIO drain (spins on FR_TXFF per byte).
    while (p_uart_buf_right != p_uart_buf_left) {
      p_uart_buf_left = (uint16_t)((p_uart_buf_left + 1) % UART_BUFFER_SIZE);
      uart_tx_raw((uint8_t)uart_buf[p_uart_buf_left]);
    }
#endif
  }
}

StatusCode uart_init(UartBaudrate baudrate)
{
  uart_task_started = false;
  return uart_hw_init(baudrate);
}

StatusCode uart_task_start(void)
{
  semaphore_init(&uart_data_ready, 1, 0);
  semaphore_init(&uart_dma_done, 1, 0);

#if UART_TX_DMA
  UART0->DMACR = DMACR_TXDMAE;                          // gate TX DREQ to the DMA
  dma_channel_init(UART_DMA_TX_CHANNEL);
  irq_register(DMA_IRQ_INTID(UART_DMA_TX_CHANNEL), uart_dma_irq_handler);
  gic_enable_spi(DMA_IRQ_INTID(UART_DMA_TX_CHANNEL), 0x80);
#endif

  // RX interrupts are always enabled here, independent of whether the app
  // ever calls uart_rx_irq_enable() — this guarantees uart_rx_irq_handler()
  // (and therefore the built-in DFU-trigger watch inside it) always runs, so
  // an app can't accidentally ship without DFU recovery support.
  // RX FIFO threshold stays at reset default (1/8); RTIM catches the tail so
  // a short burst (e.g. the 4-byte DFU key) is delivered without waiting to fill.
  UART0->ICR = ICR_ALL;                  // clear any stale latched interrupts
  UART0->IMSC |= IMSC_RXIM | IMSC_RTIM;  // enable RX-level + RX-timeout
  irq_register(UART_IRQ_INTID, uart_rx_irq_handler);
  gic_enable_spi(UART_IRQ_INTID, 0x80);

  StatusCode ret = task_create(uart_tx_task, 2048, TASK_PRIORITY_5, NULL, &uart_tcb);
  if (ret == E_OK) {
    uart_task_started = true;
  }
  return ret;
}

static void uart_tx(uint8_t byte)
{
  if (!uart_task_started) {
    uart_tx_raw(byte);
    return;
  }
  uint16_t next = (p_uart_buf_right + 1) % UART_BUFFER_SIZE;
  if (next == p_uart_buf_left) {
    return;   // buffer full, drop byte
  }
  uart_buf[next] = byte;
  p_uart_buf_right = next;
}

void uart_send_byte(uint8_t byte)
{
  if (uart_task_started) {
    uint32_t cpsr = enter_critical();
    spinlock_acquire(&uart_buf_lock);
    uart_tx(byte);
    spinlock_release(&uart_buf_lock);
    exit_critical(cpsr);
    semaphore_give(&uart_data_ready);
  }
  else {
    uart_tx_raw(byte);
  }
}

void uart_print(const char *str)
{
  if (uart_task_started) {
    uint32_t cpsr = enter_critical();
    spinlock_acquire(&uart_buf_lock);
    while (*str) {
      uart_tx((uint8_t)*str++);
    }
    spinlock_release(&uart_buf_lock);
    exit_critical(cpsr);
    semaphore_give(&uart_data_ready);
  }
  else {
    while (*str) {
      uart_tx_raw((uint8_t)*str++);
    }
  }
}

// ── Minimal mode: blocking TX, no task, no ring buffer ────────────────────────
#else

StatusCode uart_init(UartBaudrate baudrate)
{
  return uart_hw_init(baudrate);
}

static void uart_tx(uint8_t byte)
{
  uart_tx_raw(byte);
}

void uart_print(const char *str)
{
  while (*str) {
    uart_tx_raw((uint8_t)*str++);
  }
}

#endif

// ── Shared: printf (chunked sink, flushed via uart_print) ────────────────────

#define PRINTF_CHUNK_SIZE 32
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

// Small fixed-size sink that flushes to uart_print() — one critical section
// and one semaphore_give per flush — whenever it fills, instead of buffering
// an entire formatted line on the caller's stack.
typedef struct {
  char buf[PRINTF_CHUNK_SIZE];
  int pos;
} PrintfSink;

static void sink_flush(PrintfSink *sink)
{
  if (sink->pos > 0) {
    sink->buf[sink->pos] = '\0';
    uart_print(sink->buf);
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

void uart_printf(const char *fmt, ...)
{
  PrintfSink sink = { .pos = 0 };

  va_list args;
  va_start(args, fmt);

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

  va_end(args);
  sink_flush(&sink);
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

  uart_tx_raw('P'); uart_tx_raw('C'); uart_tx_raw('=');
  fault_puthex(pc);

  uart_tx_raw(' '); uart_tx_raw('A'); uart_tx_raw('=');
  fault_puthex(addr);

  uart_tx_raw(' '); uart_tx_raw('S'); uart_tx_raw('=');
  fault_puthex(status);

  uart_tx_raw(' '); uart_tx_raw('K'); uart_tx_raw('=');
  fault_puthex(kind);

  uart_tx_raw('\r');
  uart_tx_raw('\n');
}
