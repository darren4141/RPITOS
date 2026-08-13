#ifndef I2C_H
#define I2C_H

#include <stdbool.h>
#include <stdint.h>

#include "gpio.h"
#include "status.h"

#define I2C0_BASE                     0xFE205000
#define I2C1_BASE                     0xFE804000
#define I2C3_BASE                     0xFE205600
#define I2C4_BASE                     0xFE205800
#define I2C5_BASE                     0xFE205A00
#define I2C6_BASE                     0xFE205C00

typedef struct {
  volatile uint32_t C;                          // 0x00 — control (I2CEN, INTR/INTT/INTD, ST, CLEAR, READ)
  volatile uint32_t S;                          // 0x04 — status (TA, DONE, TXW, RXR, TXD, RXD, TXE, RXF, ERR, CLKT)
  volatile uint32_t DLEN;                       // 0x08 — data length
  volatile uint32_t A;                          // 0x0C — slave address
  volatile uint32_t FIFO;                       // 0x10 — data FIFO (TX/RX)
  volatile uint32_t DIV;                        // 0x14 — clock divider
  volatile uint32_t DEL;                        // 0x18 — data hold/setup delay
  volatile uint32_t CLKT;                       // 0x1C — clock stretch timeout
} BSCRegs;

// C — control register
#define C_I2CEN                       (1 << 15) // I2C enable
#define C_INTR                        (1 << 10) // interrupt while RX FIFO needs service
#define C_INTT                        (1 << 9)  // interrupt while TX FIFO needs service
#define C_INTD                        (1 << 8)  // interrupt on transfer DONE
#define C_ST                          (1 << 7)  // start a new transfer
#define C_CLEAR                       (1 << 4)  // clear FIFO (bits 4-5 both clear; bit 4 alone is sufficient)
#define C_READ                        (1 << 0)  // 1 = read transfer, 0 = write transfer
#define C_WRITE                       (0 << 0)  // 1 = read transfer, 0 = write transfer

// S — status register (CLKT/ERR/DONE are write-1-to-clear)
#define S_CLKT                        (1 << 9)  // clock stretch timeout
#define S_ERR                         (1 << 8)  // slave NACK'd the address or a data byte
#define S_RXF                         (1 << 7)  // RX FIFO full
#define S_TXE                         (1 << 6)  // TX FIFO empty
#define S_RXD                         (1 << 5)  // RX FIFO contains at least one byte
#define S_TXD                         (1 << 4)  // TX FIFO has space for at least one byte
#define S_RXR                         (1 << 3)  // RX FIFO needs reading (at/above threshold)
#define S_TXW                         (1 << 2)  // TX FIFO needs writing (at/below threshold)
#define S_DONE                        (1 << 1)  // transfer complete
#define S_TA                          (1 << 0)  // transfer active

// Clear CLKT/ERR/DONE before starting a transfer — see docs.md's "Transfer flow".
#define S_CLEAR_ALL                   (S_CLKT | S_ERR | S_DONE)

// FIFO depth, bytes — the classic Broadcom BSC engine (same IP across all Pi
// generations). i2c_channel_write_read()'s repeated-start technique needs the
// whole tx payload to fit in one FIFO load — see docs.md's "Repeated start".
#define I2C_FIFO_DEPTH                16U

// DEL — data hold/setup delay: two packed 16-bit fields (core-clock cycles), not a bitmask
#define DEL_FEDL_SHIFT                16 // falling-edge delay: SCL falling edge → SDA change
#define DEL_REDL_SHIFT                0  // rising-edge delay: SCL rising edge → data sample

typedef enum {
  I2C_BAUDRATE_STANDARD_100K,
  I2C_BAUDRATE_FAST_400K,
} I2cBaudrate;

#define I2C_BAUDRATE_STANDARD_100K_HZ 100000U
#define I2C_BAUDRATE_FAST_400K_HZ     400000U

#define I2C_CHANNEL_0                 0U
#define I2C_CHANNEL_1                 1U
#define I2C_CHANNEL_3                 3U
#define I2C_CHANNEL_4                 4U
#define I2C_CHANNEL_5                 5U
#define I2C_CHANNEL_6                 6U

#define I2C_NUM_CHANNELS              7U

// Every BSC node in the DTB lists this same SPI 117 → GIC INTID 149
#define I2C_IRQ_INTID                 149

typedef struct {
  uint8_t sda_pin;
  uint8_t scl_pin;
  GPIOFunc alt_func;
  I2cBaudrate baudrate;
} I2cConfig;

/**
 * @brief Configure an I2C channel's GPIO pins, pull-ups, and bus speed.
 * @note channel is the literal BCM2711 I2C number (0, 1, 3-6; 2 is HDMI-dedicated, unsupported).
 */
StatusCode i2c_channel_init(uint8_t channel, I2cConfig *config);

/**
 * @brief Disable the channel and release its GPIO pins.
 */
void i2c_channel_deinit(uint8_t channel);

/**
 * @brief Write len bytes to the 7-bit slave address addr, blocking until done.
 */
StatusCode i2c_channel_write(uint8_t channel, uint8_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief Read len bytes from the 7-bit slave address addr, blocking until done.
 */
StatusCode i2c_channel_read(uint8_t channel, uint8_t addr, uint8_t *buf, uint16_t len);

/**
 * @brief Write tx_buf then, with a repeated START (no STOP in between), read rx_len bytes into rx_buf.
 * @note The standard "read register N" I2C idiom most peripherals expect — not equivalent to
 * two independent i2c_channel_write()/i2c_channel_read() calls, which would insert a STOP.
 * @note The BSC has no hardware repeated-start; this re-arms C.ST while TA is
 * still asserted from the write phase — see docs.md's "Repeated start".
 * tx_len must fit in one FIFO load (<= I2C_FIFO_DEPTH); returns E_INVALID_ARGS otherwise.
 */
StatusCode i2c_channel_write_read(uint8_t channel, uint8_t addr,
                                  const uint8_t *tx_buf, uint16_t tx_len,
                                  uint8_t *rx_buf, uint16_t rx_len);

#endif