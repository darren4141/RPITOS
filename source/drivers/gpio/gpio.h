#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#define GPIO_BASE 0xFE200000UL

typedef enum {
  GPIO_FUNC_INPUT  = 0b000,      // 0
  GPIO_FUNC_OUTPUT = 0b001,      // 1
  GPIO_FUNC_ALT0   = 0b100,      // 4
  GPIO_FUNC_ALT1   = 0b101,      // 5
  GPIO_FUNC_ALT2   = 0b110,      // 6
  GPIO_FUNC_ALT3   = 0b111,      // 7
  GPIO_FUNC_ALT4   = 0b011,      // 3
  GPIO_FUNC_ALT5   = 0b010,      // 2
} GPIOFunc;

typedef enum {
  GPIO_PULL_NONE = 0b00,
  GPIO_PULL_UP   = 0b01,
  GPIO_PULL_DOWN = 0b10,
} GPIOPull;

typedef struct {
  volatile uint32_t GPFSEL[6];   // 0x00–0x14  function select (6 regs, 10 pins each)
  volatile uint32_t PAD0;        // 0x18        reserved
  volatile uint32_t GPSET[2];    // 0x1C–0x20  pin output set    (1 bit per pin)
  volatile uint32_t PAD1;        // 0x24        reserved
  volatile uint32_t GPCLR[2];    // 0x28–0x2C  pin output clear  (1 bit per pin)
  volatile uint32_t PAD2;        // 0x30        reserved
  volatile uint32_t GPLEV[2];    // 0x34–0x38  pin level read    (1 bit per pin)
  volatile uint32_t PAD3;        // 0x3C        reserved
  volatile uint32_t GPEDS[2];    // 0x40–0x44  event detect status
  volatile uint32_t PAD4;        // 0x48        reserved
  volatile uint32_t GPREN[2];    // 0x4C–0x50  rising edge detect enable
  volatile uint32_t PAD5;        // 0x54        reserved
  volatile uint32_t GPFEN[2];    // 0x58–0x5C  falling edge detect enable
  volatile uint32_t PAD6;        // 0x60        reserved
  volatile uint32_t GPHEN[2];    // 0x64–0x68  high detect enable
  volatile uint32_t PAD7;        // 0x6C        reserved
  volatile uint32_t GPLEN[2];    // 0x70–0x74  low detect enable
  volatile uint32_t PAD8;        // 0x78        reserved
  volatile uint32_t GPAREN[2];   // 0x7C–0x80  async rising edge detect
  volatile uint32_t PAD9;        // 0x84        reserved
  volatile uint32_t GPAFEN[2];   // 0x88–0x8C  async falling edge detect
  volatile uint32_t PAD10[21];   // 0x90–0xE0  reserved (includes legacy GPPUD)
  volatile uint32_t GPPUPPDN[4]; // 0xE4–0xF0  pull up/down control (BCM2711)
} GPIORegs;

#define GPIO ((GPIORegs *)GPIO_BASE)

/**
 * @brief Set a pin's alternate function (input, output, or ALT0–ALT5).
 */
void gpio_set_function(uint32_t pin, GPIOFunc funct);

/**
 * @brief Set a pin's internal pull-up/pull-down/none.
 */
void gpio_set_pull(uint8_t pin, GPIOPull pull);

/**
 * @brief Drive a pin high.
 */
void gpio_on(uint32_t pin);

/**
 * @brief Drive a pin low.
 */
void gpio_off(uint32_t pin);

/**
 * @brief Read a pin's current input level.
 */
uint8_t gpio_read(uint32_t pin);

#endif
