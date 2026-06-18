#ifndef UART_H
#define UART_H

#include <stdint.h>

#include "gpio.h"
#include "status.h"

#define UART0_BASE       0xFE201000UL

typedef struct {
  volatile uint32_t DR;           // 0x00 — data register (TX/RX)
  volatile uint32_t RSRECR;       // 0x04 — receive status / error clear
  volatile uint32_t PAD[4];       // 0x08–0x14 — reserved
  volatile uint32_t FR;           // 0x18 — flag register (TX/RX ready/busy)
  volatile uint32_t PAD2;         // 0x1C — reserved
  volatile uint32_t ILPR;         // 0x20 — IrDA low power (unused)
  volatile uint32_t IBRD;         // 0x24 — integer baud rate divisor
  volatile uint32_t FBRD;         // 0x28 — fractional baud rate divisor
  volatile uint32_t LCRH;         // 0x2C — line control (data bits, FIFO, parity)
  volatile uint32_t CR;           // 0x30 — control (enable TX/RX)
  volatile uint32_t IFLS;         // 0x34 — FIFO interrupt level select
  volatile uint32_t IMSC;         // 0x38 — interrupt mask set/clear
  volatile uint32_t RIS;          // 0x3C — raw interrupt status
  volatile uint32_t MIS;          // 0x40 — masked interrupt status
  volatile uint32_t ICR;          // 0x44 — interrupt clear register
  volatile uint32_t DMACR;        // 0x48 — DMA control (unused)
} PL011Regs_t;

// FR — flag register
#define FR_TXFF          (1 << 5) // TX FIFO full  — don't write if set
#define FR_RXFE          (1 << 4) // RX FIFO empty — don't read if set
#define FR_BUSY          (1 << 3) // TX busy       — wait before disabling

// LCRH — line control
#define LCRH_WLEN_8      (3 << 5) // 8 data bits
#define LCRH_WLEN_7      (2 << 5) // 7 data bits
#define LCRH_FEN         (1 << 4) // enable FIFOs
#define LCRH_STP2        (1 << 3) // 2 stop bits (0 = 1 stop bit)
#define LCRH_PEN         (1 << 1) // parity enable

// CR — control register
#define CR_RXE           (1 << 9) // RX enable
#define CR_TXE           (1 << 8) // TX enable
#define CR_UARTEN        (1 << 0) // UART enable

// IMSC — interrupt masks
#define IMSC_RXIM        (1 << 4) // RX interrupt mask
#define IMSC_TXIM        (1 << 5) // TX interrupt mask

// ICR — interrupt clear
#define ICR_ALL          0x7FF    // clear all interrupts

#define UART_CLK         48000000

#define UART_IBRD_115200 26
#define UART_FBRD_115200 3

typedef enum {
  UART_BAUDRATE_115200
} UartBaudrate;

#define UART0            ((PL011Regs_t *)UART0_BASE)

#define UART_BUFFER_SIZE 1024

StatusCode uart_init(UartBaudrate baudrate);
#ifndef UART_MINIMAL
StatusCode uart_task_start(void);
#endif
void uart_tx(uint8_t byte);
uint8_t uart_rx();
StatusCode uart_rx_nonblocking(uint8_t *out);
void uart_print(const char *str);
void uart_printf(const char *fmt, ...);


#endif