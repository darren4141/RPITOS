#ifndef I2C_H
#define I2C_H

#include <stdbool.h>
#include <stdint.h>

#include "gpio.h"
#include "status.h"

#define I2C0_BASE        0xFE205000
#define I2C1_BASE        0xFE804000
#define I2C3_BASE        0xFE205600
#define I2C4_BASE        0xFE205800
#define I2C5_BASE        0xFE205A00
#define I2C6_BASE        0xFE205C00

typedef struct {
  volatile uint32_t C;      // 0x00 — control (I2CEN, INTR/INTT/INTD, ST, CLEAR, READ)
  volatile uint32_t S;      // 0x04 — status (TA, DONE, TXW, RXR, TXD, RXD, TXE, RXF, ERR, CLKT)
  volatile uint32_t DLEN;   // 0x08 — data length
  volatile uint32_t A;      // 0x0C — slave address
  volatile uint32_t FIFO;   // 0x10 — data FIFO (TX/RX)
  volatile uint32_t DIV;    // 0x14 — clock divider
  volatile uint32_t DEL;    // 0x18 — data hold/setup delay
  volatile uint32_t CLKT;   // 0x1C — clock stretch timeout
} BSCRegs;

typedef enum {
  I2C_BAUDRATE_STANDARD_100K,
  I2C_BAUDRATE_FAST_400K,
} I2cBaudrate;

#define I2C_CHANNEL_0    0U
#define I2C_CHANNEL_1    1U
#define I2C_CHANNEL_3    3U
#define I2C_CHANNEL_4    4U
#define I2C_CHANNEL_5    5U
#define I2C_CHANNEL_6    6U

#define I2C_NUM_CHANNELS 7U

// Every BSC node in the DTB lists this same SPI 117 → GIC INTID 149
#define I2C_IRQ_INTID    149

typedef struct {
  uint8_t sda_pin;
  uint8_t scl_pin;
  GPIOFunc alt_func;
  I2cBaudrate baudrate;
} I2cConfig;

#endif