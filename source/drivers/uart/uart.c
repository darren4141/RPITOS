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

static TaskControlBlock *uart_tcb = NULL;
static bool uart_task_started = false;

#if UART_TX_DMA
static DmaControlBlock_t uart_tx_cb __attribute__((aligned(32)));

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
  volatile DmaChannelRegs_t *ch = DMA_CHANNEL(UART_DMA_TX_CHANNEL);
  ch->CS = DMA_CS_INT;   // write-1-to-clear the channel interrupt latch
  __asm__ volatile ("dsb sy" ::: "memory");
  semaphore_give(&uart_dma_done);
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
  gic_enable_spi(DMA_IRQ_INTID(UART_DMA_TX_CHANNEL), 0x80);
#endif

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
    uart_tx(byte);
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
    while (*str) {
      uart_tx((uint8_t)*str++);
    }
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

// ── Shared: printf (both modes write to a buffer, then flush via uart_print) ──

static int format_uint(char *out, uint32_t n, uint32_t base, const char *digits, int width, char pad)
{
  char tmp[10];
  int i = 0;
  if (n == 0) {
    tmp[i++] = '0';
  }
  else {
    while (n > 0) { tmp[i++] = digits[n % base];n /= base; }
  }
  int len = 0;
  for (int p = i; p < width; p++) {
    out[len++] = pad;
  }
  while (i > 0) {out[len++] = tmp[--i];}
  return len;
}

void uart_printf(const char *fmt, ...)
{
  char local[256];
  int pos = 0;

  va_list args;
  va_start(args, fmt);

  while (*fmt) {
    if (*fmt != '%') {
      if (pos < (int)sizeof(local) - 1) {
        local[pos++] = *fmt;
      }
      fmt++;
      continue;
    }
    fmt++;
    char pad = ' ';
    if (*fmt == '0') {
      pad = '0';fmt++;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }
    switch (*fmt) {
    case 'c':
      if (pos < (int)sizeof(local) - 1) {
        local[pos++] = (char)va_arg(args, int);
      }
      break;

    case 's': {
      const char *s = va_arg(args, const char *);
      if (!s) {
        s = "(null)";
      }
      while (*s && pos < (int)sizeof(local) - 1) {local[pos++] = *s++;}
      break;
    }

    case 'd': {
      int32_t n = va_arg(args, int32_t);
      if (pos + 12 < (int)sizeof(local)) {
        if (n < 0) {
          local[pos++] = '-';pos += format_uint(local + pos, (uint32_t)-n, 10, "0123456789", (width > 0) ? width - 1 : 0, pad);
        }
        else {
          pos += format_uint(local + pos, (uint32_t)n, 10, "0123456789", width, pad);
        }
      }
      break;
    }

    case 'u':
      if (pos + 12 < (int)sizeof(local)) {
        pos += format_uint(local + pos, va_arg(args, uint32_t), 10, "0123456789", width, pad);
      }
      break;

    case 'x':
      if (pos + 12 < (int)sizeof(local)) {
        pos += format_uint(local + pos, va_arg(args, uint32_t), 16, "0123456789abcdef", width, pad);
      }
      break;

    case 'X':
      if (pos + 12 < (int)sizeof(local)) {
        pos += format_uint(local + pos, va_arg(args, uint32_t), 16, "0123456789ABCDEF", width, pad);
      }
      break;

    case '%':
      if (pos < (int)sizeof(local) - 1) {
        local[pos++] = '%';
      }
      break;

    default:
      if (pos + 1 < (int)sizeof(local) - 1) {
        local[pos++] = '%';local[pos++] = *fmt;
      }
      break;
    }
    fmt++;
  }

  va_end(args);
  local[pos] = '\0';
  uart_print(local);
}
