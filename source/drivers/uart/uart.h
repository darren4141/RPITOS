#ifndef UART_H
#define UART_H

#include <stdint.h>

#include "gpio.h"
#include "status.h"

#define UART0_BASE          0xFE201000UL

typedef struct {
  volatile uint32_t DR;              // 0x00 — data register (TX/RX)
  volatile uint32_t RSRECR;          // 0x04 — receive status / error clear
  volatile uint32_t PAD[4];          // 0x08–0x14 — reserved
  volatile uint32_t FR;              // 0x18 — flag register (TX/RX ready/busy)
  volatile uint32_t PAD2;            // 0x1C — reserved
  volatile uint32_t ILPR;            // 0x20 — IrDA low power (unused)
  volatile uint32_t IBRD;            // 0x24 — integer baud rate divisor
  volatile uint32_t FBRD;            // 0x28 — fractional baud rate divisor
  volatile uint32_t LCRH;            // 0x2C — line control (data bits, FIFO, parity)
  volatile uint32_t CR;              // 0x30 — control (enable TX/RX)
  volatile uint32_t IFLS;            // 0x34 — FIFO interrupt level select
  volatile uint32_t IMSC;            // 0x38 — interrupt mask set/clear
  volatile uint32_t RIS;             // 0x3C — raw interrupt status
  volatile uint32_t MIS;             // 0x40 — masked interrupt status
  volatile uint32_t ICR;             // 0x44 — interrupt clear register
  volatile uint32_t DMACR;           // 0x48 — DMA control (unused)
} PL011Regs;

// FR — flag register
#define FR_TXFF             (1 << 5) // TX FIFO full  — don't write if set
#define FR_RXFE             (1 << 4) // RX FIFO empty — don't read if set
#define FR_BUSY             (1 << 3) // TX busy       — wait before disabling

// LCRH — line control
#define LCRH_WLEN_8         (3 << 5) // 8 data bits
#define LCRH_WLEN_7         (2 << 5) // 7 data bits
#define LCRH_FEN            (1 << 4) // enable FIFOs
#define LCRH_STP2           (1 << 3) // 2 stop bits (0 = 1 stop bit)
#define LCRH_PEN            (1 << 1) // parity enable

// CR — control register
#define CR_RXE              (1 << 9) // RX enable
#define CR_TXE              (1 << 8) // TX enable
#define CR_UARTEN           (1 << 0) // UART enable

// IMSC — interrupt masks
#define IMSC_RXIM           (1 << 4) // RX FIFO ≥ threshold interrupt mask
#define IMSC_TXIM           (1 << 5) // TX interrupt mask
#define IMSC_RTIM           (1 << 6) // RX timeout interrupt mask (catches FIFO tail)

// ICR — interrupt clear (write 1 to clear)
#define ICR_ALL             0x7FF    // clear all interrupts
#define ICR_RXIC            (1 << 4) // clear RX interrupt
#define ICR_RTIC            (1 << 6) // clear RX timeout interrupt

// DMACR — DMA control
#define DMACR_RXDMAE        (1 << 0) // RX DMA enable
#define DMACR_TXDMAE        (1 << 1) // TX DMA enable
#define DMACR_DMAONERR      (1 << 2) // disable DMA on RX error

// VideoCore bus alias of UART0->DR — see docs.md.
#define UART0_DR_BUS        0x7E201000UL

// DMA Lite channel used for UART TX — see docs.md before changing.
#define UART_DMA_TX_CHANNEL 7

// PL011 UART0 combined interrupt → GIC INTID on BCM2711 — see docs.md before changing.
#define UART_IRQ_INTID      153

// See docs.md for what these build-time flags do.
#ifndef UART_MINIMAL
#ifndef UART_TX_DMA
#define UART_TX_DMA         1
#endif

#ifndef UART_TX_TIMING
#define UART_TX_TIMING      0
#endif
#endif

#define UART_CLK            48000000

#define UART_IBRD_115200    26
#define UART_FBRD_115200    3

typedef enum {
  UART_BAUDRATE_115200
} UartBaudrate;

#define UART0               ((PL011Regs *)UART0_BASE)

#define UART_BUFFER_SIZE    2056

/**
 * @brief Configure the PL011 UART's GPIO pins, baud rate, and line control.
 */
StatusCode uart_init(UartBaudrate baudrate);

/**
 * @brief Drain the TX FIFO, disable the UART, and release its GPIO pins.
 */
void uart_deinit();

/**
 * @brief Block until the TX FIFO has fully drained.
 */
void uart_drain(void);

/**
 * @brief Transmit a single raw byte, blocking until the TX FIFO has room.
 */
void uart_tx_raw(uint8_t byte);

#ifndef UART_MINIMAL

/**
 * @brief Start the ring-buffer TX task and enable RX interrupts (full mode only).
 */
StatusCode uart_task_start(void);

/**
 * @brief Queue a byte for transmission via the ring buffer (full mode only).
 */
void uart_send_byte(uint8_t byte);

/**
 * @brief TX-complete handler for the UART DMA channel.
 * @note Called from _irq_handler on DMA TX completion.
 */
void uart_dma_irq_handler(void);

/**
 * @brief Signature for an app-specific RX byte callback.
 * @note Invoked from IRQ context for each received byte.
 */
typedef void (*UartRxHandler)(uint8_t byte);

/**
 * @brief Register an additional app-specific RX handler, layered on top of the built-in DFU-trigger watch.
 * @note uart_task_start() already enables RX interrupts and the DFU-trigger
 * watch unconditionally — this is optional; an app that doesn't need custom
 * RX handling doesn't need to call it.
 */
void uart_rx_irq_enable(UartRxHandler handler);

/**
 * @brief RX interrupt handler: drains the RX FIFO, feeds the DFU-trigger watch, and calls the registered app handler.
 * @note Called from _irq_handler on the PL011 combined interrupt.
 */
void uart_rx_irq_handler(void);

#if UART_TX_TIMING
/**
 * @brief Print the accumulated TX timing snapshot (UART_TX_TIMING builds only).
 */
void uart_tx_timing_report(void);

/**
 * @brief Zero the TX timing accumulators (UART_TX_TIMING builds only).
 */
void uart_tx_timing_reset(void);

/**
 * @brief Return the CPU-active cycles spent in TX so far (UART_TX_TIMING builds only).
 */
uint64_t uart_tx_get_active_cycles(void);

/**
 * @brief Return the number of bytes transmitted so far (UART_TX_TIMING builds only).
 */
uint64_t uart_tx_get_byte_count(void);
#endif
#endif

/**
 * @brief Read one byte, blocking until the RX FIFO is non-empty.
 */
uint8_t uart_rx();

/**
 * @brief Read one byte if available, without blocking.
 * @return E_EMPTY if the RX FIFO is empty.
 */
StatusCode uart_rx_nonblocking(uint8_t *out);

/**
 * @brief Read one byte, blocking up to timeout_ms.
 * @return E_TIMED_OUT if no byte arrives in time.
 */
StatusCode uart_rx_timed(uint8_t *out, uint32_t timeout_ms);

/**
 * @brief Write a NUL-terminated string.
 */
void uart_print(const char *str);

/**
 * @brief Write a printf-style formatted string. Supports %c %s %d %u %x %X %% with zero-padding and width.
 */
void uart_printf(const char *fmt, ...);

/**
 * @brief Print a fixed-format fault report (PC, fault address, status, kind) from a context where the normal printf path may not be safe.
 */
void uart_fault_report(uint32_t kind, uint32_t pc, uint32_t addr, uint32_t status);

#endif
