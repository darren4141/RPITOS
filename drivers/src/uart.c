#include "uart.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef UART_MINIMAL
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

static void uart_tx_raw(uint8_t byte)
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
  asm volatile ("mrc  p15, 0, %0, c14, c0, 0" : "=r"(frq));
  asm volatile ("mrrc p15, 0, %0, %1,  c14"   : "=r"(lo), "=r"(hi));
  uint64_t start = ((uint64_t)hi << 32) | lo;
  uint64_t ticks = (uint64_t)frq * timeout_ms / 1000ULL;
  while (UART0->FR & FR_RXFE) {
    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    if ((((uint64_t)hi << 32) | lo) - start >= ticks) {
      return E_TIMED_OUT;
    }
  }
  *out = (uint8_t)(UART0->DR & 0xFF);
  return E_OK;
}

void uart_deinit()
{
  // Drain TX FIFO before touching control registers — disabling mid-byte
  // corrupts the current character on the wire.
  while (UART0->FR & FR_BUSY) {}

  UART0->CR &= ~(CR_UARTEN | CR_TXE | CR_RXE);   // disable UART, TX, RX
  UART0->LCRH &= ~LCRH_FEN;                      // flush + disable FIFO
  UART0->IMSC = 0;                               // mask all interrupts
  UART0->ICR = ICR_ALL;                          // clear any pending

  gpio_set_function(14, GPIO_FUNC_INPUT);
  gpio_set_function(15, GPIO_FUNC_INPUT);
}

// ── Full mode: ring-buffer TX + scheduler task ────────────────────────────────
#ifndef UART_MINIMAL

static volatile uint8_t uart_buf[UART_BUFFER_SIZE];
static volatile uint16_t p_uart_buf_left = 0;
static volatile uint16_t p_uart_buf_right = 0;

static TaskControlBlock *uart_tcb = NULL;
static bool uart_task_started = false;

void uart_tx_task(void *params)
{
  while (1) {
    if (p_uart_buf_right != p_uart_buf_left) {
      p_uart_buf_left++;
      if (p_uart_buf_left >= UART_BUFFER_SIZE) {
        p_uart_buf_left = 0;
      }
      uart_tx_raw(uart_buf[p_uart_buf_left]);
    }
  }
}

StatusCode uart_init(UartBaudrate baudrate)
{
  uart_task_started = false;
  return uart_hw_init(baudrate);
}

StatusCode uart_task_start(void)
{
  StatusCode ret = task_create(uart_tx_task, 512, TASK_PRIORITY_2, NULL, &uart_tcb);
  if (ret == E_OK) {
    uart_task_started = true;
  }
  return ret;
}

void uart_tx(uint8_t byte)
{
  if (!uart_task_started) {
    uart_tx_raw(byte);
    return;
  }
  uint16_t next = p_uart_buf_right + 1;
  if (next >= UART_BUFFER_SIZE) {
    next = 0;
  }
  uart_buf[next] = byte;
  p_uart_buf_right = next;
}

void uart_print(const char *str)
{
  if (uart_task_started) {
    __asm__ volatile ("cpsid i" ::: "memory");
  }
  while (*str) {
    uart_tx((uint8_t)*str++);
  }
  if (uart_task_started) {
    __asm__ volatile ("cpsie i" ::: "memory");
  }
}

static void print_uint(uint32_t n, uint32_t base, const char *digits, int width, char pad)
{
  char buf[10];
  int i = 0;
  if (n == 0) {
    buf[i++] = '0';
  }
  else {
    while (n > 0) { buf[i++] = digits[n % base];n /= base; }
  }
  for (int p = i; p < width; p++) {
    uart_tx((uint8_t)pad);
  }
  while (i > 0) { uart_tx((uint8_t)buf[--i]); }
}

void uart_printf(const char *fmt, ...)
{
  if (uart_task_started) {
    __asm__ volatile ("cpsid i" ::: "memory");
  }
  va_list args;
  va_start(args, fmt);

  while (*fmt) {
    if (*fmt != '%') {
      uart_tx((uint8_t)*fmt++);continue;
    }
    fmt++;
    char pad = ' ';
    if (*fmt == '0') {
      pad = '0';fmt++;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }
    switch (*fmt) {
    case 'c': uart_tx((uint8_t)va_arg(args, int));break;

    case 's': { const char *s = va_arg(args, const char *);uart_print(s ? s : "(null)");break; }

    case 'd': { int32_t n = va_arg(args, int32_t);
                if (n < 0) {
                  uart_tx('-');print_uint((uint32_t)-n, 10, "0123456789", width > 0 ? width - 1 : 0, pad);
                }
                else {
                  print_uint((uint32_t)n, 10, "0123456789", width, pad);
                } break; }

    case 'u': print_uint(va_arg(args, uint32_t), 10, "0123456789", width, pad);break;

    case 'x': print_uint(va_arg(args, uint32_t), 16, "0123456789abcdef", width, pad);break;

    case 'X': print_uint(va_arg(args, uint32_t), 16, "0123456789ABCDEF", width, pad);break;

    case '%': uart_tx('%');break;

    default:  uart_tx('%');uart_tx((uint8_t)*fmt);break;
    }
    fmt++;
  }

  va_end(args);
  if (uart_task_started) {
    __asm__ volatile ("cpsie i" ::: "memory");
  }
}

// ── Minimal mode: blocking TX, no task, no ring buffer ────────────────────────
// Minimal mode is used by the bootloader since it has no RTOS and cannot support a UART task & buffer
#else

StatusCode uart_init(UartBaudrate baudrate)
{
  return uart_hw_init(baudrate);
}

void uart_tx(uint8_t byte)
{
  uart_tx_raw(byte);
}

void uart_print(const char *str)
{
  while (*str) {
    uart_tx_raw((uint8_t)*str++);
  }
}

static void print_uint(uint32_t n, uint32_t base, const char *digits, int width, char pad)
{
  char buf[10];
  int i = 0;
  if (n == 0) {
    buf[i++] = '0';
  }
  else {
    while (n > 0) { buf[i++] = digits[n % base];n /= base; }
  }
  for (int p = i; p < width; p++) {
    uart_tx_raw((uint8_t)pad);
  }
  while (i > 0) { uart_tx_raw((uint8_t)buf[--i]); }
}

void uart_printf(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  while (*fmt) {
    if (*fmt != '%') {
      uart_tx_raw((uint8_t)*fmt++);continue;
    }
    fmt++;
    char pad = ' ';
    if (*fmt == '0') {
      pad = '0';fmt++;
    }
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt++ - '0'); }
    switch (*fmt) {
    case 'c': uart_tx_raw((uint8_t)va_arg(args, int));break;

    case 's': { const char *s = va_arg(args, const char *);uart_print(s ? s : "(null)");break; }

    case 'd': { int32_t n = va_arg(args, int32_t);
                if (n < 0) {
                  uart_tx_raw('-');print_uint((uint32_t)-n, 10, "0123456789", width > 0 ? width - 1 : 0, pad);
                }
                else {
                  print_uint((uint32_t)n, 10, "0123456789", width, pad);
                } break; }

    case 'u': print_uint(va_arg(args, uint32_t), 10, "0123456789", width, pad);break;

    case 'x': print_uint(va_arg(args, uint32_t), 16, "0123456789abcdef", width, pad);break;

    case 'X': print_uint(va_arg(args, uint32_t), 16, "0123456789ABCDEF", width, pad);break;

    case '%': uart_tx_raw('%');break;

    default:  uart_tx_raw('%');uart_tx_raw((uint8_t)*fmt);break;
    }
    fmt++;
  }

  va_end(args);
}

#endif
