#include "task_types.h"
#include "uart.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "task.h"

static volatile uint8_t uart_buf[UART_BUFFER_SIZE];
static volatile uint16_t p_uart_buf_left = 0;
static volatile uint16_t p_uart_buf_right = 0;

static TaskControlBlock *uart_tcb = NULL;

static void uart_tx_raw(uint8_t byte);

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
  StatusCode ret = uart_hw_init(baudrate);
  if (ret != E_OK) {
    return ret;
  }
  ret = task_create(uart_tx_task, 512, TASK_PRIORITY_5, NULL, &uart_tcb);
  if (ret != E_OK) {
    return ret;
  }

  return E_OK;
}

void uart_tx_raw(uint8_t byte)
{
  while (UART0->FR & FR_TXFF) {
  }
  UART0->DR = byte;
}

void uart_tx(uint8_t byte)
{
  uint16_t next = p_uart_buf_right + 1;
  if (next >= UART_BUFFER_SIZE) {
    next = 0;
  }
  uart_buf[next] = byte;     // write before making slot visible
  p_uart_buf_right = next;
}

uint8_t uart_rx(uint8_t byte)
{
  while (UART0->FR & FR_RXFE) {
  }
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

void uart_print(const char *str)
{
  __asm__ volatile ("cpsid i" ::: "memory");
  while (*str) {
    uart_tx((uint8_t)*str++);
  }
  __asm__ volatile ("cpsie i" ::: "memory");
}

static void print_uint(uint32_t n, uint32_t base, const char *digits)
{
  char buf[10];
  int i = 0;

  if (n == 0) {
    uart_tx('0');
    return;
  }

  while (n > 0) {
    buf[i++] = digits[n % base];
    n /= base;
  }

  while (i > 0) {
    uart_tx((uint8_t)buf[--i]);
  }
}

void uart_printf(const char *fmt, ...)
{
  __asm__ volatile ("cpsid i" ::: "memory");
  va_list args;
  va_start(args, fmt);

  while (*fmt) {
    if (*fmt != '%') {
      uart_tx((uint8_t)*fmt++);
      continue;
    }

    fmt++;
    switch (*fmt) {
    case 'c':
      uart_tx((uint8_t)va_arg(args, int));
      break;

    case 's': {
      const char *s = va_arg(args, const char *);
      uart_print(s ? s : "(null)");
      break;
    }

    case 'd': {
      int32_t n = va_arg(args, int32_t);
      if (n < 0) {
        uart_tx('-');
        print_uint((uint32_t)-n, 10, "0123456789");
      }
      else {
        print_uint((uint32_t)n, 10, "0123456789");
      }
      break;
    }

    case 'u':
      print_uint(va_arg(args, uint32_t), 10, "0123456789");
      break;

    case 'x':
      print_uint(va_arg(args, uint32_t), 16, "0123456789abcdef");
      break;

    case 'X':
      print_uint(va_arg(args, uint32_t), 16, "0123456789ABCDEF");
      break;

    case '%':
      uart_tx('%');
      break;

    default:
      uart_tx('%');
      uart_tx((uint8_t)*fmt);
      break;
    }
    fmt++;
  }

  va_end(args);
  __asm__ volatile ("cpsie i" ::: "memory");
}
