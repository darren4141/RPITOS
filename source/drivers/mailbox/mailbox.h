#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>

#include "status.h"

// VideoCore mailbox, property-tag channel — the standard ARM<->VC RPC
// mechanism for clock rates, memory splits, board info, framebuffer setup,
// etc. See docs.md for the wire format.

#define MBOX_CH_PROPERTY               8U   // ARM->VC property-tag channel

// Property tag IDs used by this driver's convenience wrappers. The full
// catalog is much larger (framebuffer, power, virtual GPIO, ...) — add tags
// here as callers need them; mbox_property_call() works with any of them.
#define MBOX_TAG_GET_FIRMWARE_REVISION 0x00000001U
#define MBOX_TAG_GET_BOARD_SERIAL      0x00010004U
#define MBOX_TAG_GET_ARM_MEMORY        0x00010005U
#define MBOX_TAG_GET_CLOCK_RATE        0x00030002U

// Mailbox clock IDs, for use with MBOX_TAG_GET_CLOCK_RATE / mbox_get_clock_rate().
// There is no distinct ID for the I2C/BSC block — see i2c/docs.md.
#define MBOX_CLOCK_ID_EMMC             1U
#define MBOX_CLOCK_ID_UART             2U
#define MBOX_CLOCK_ID_ARM              3U
#define MBOX_CLOCK_ID_CORE             4U
#define MBOX_CLOCK_ID_V3D              5U
#define MBOX_CLOCK_ID_H264             6U
#define MBOX_CLOCK_ID_ISP              7U
#define MBOX_CLOCK_ID_SDRAM            8U
#define MBOX_CLOCK_ID_PIXEL            9U
#define MBOX_CLOCK_ID_PWM              10U
#define MBOX_CLOCK_ID_HEVC             11U
#define MBOX_CLOCK_ID_EMMC2            12U
#define MBOX_CLOCK_ID_M2MC             13U
#define MBOX_CLOCK_ID_PIXEL_BVB        14U

/**
 * @brief Send a raw property-tag request buffer and block until the VC responds.
 * @note buf must be 16-byte aligned, hold a complete property-tag message
 * (size header, request code, one or more tags, 0 end tag), and live in
 * normal RAM — no cache maintenance needed, this kernel runs with D-cache
 * disabled globally (see startup.s). Overwritten with the VC's response in place.
 * @return E_OK, E_TIMED_OUT (mailbox never freed up / VC never responded), or
 * E_CORRUPTED (VC reported the request failed).
 */
StatusCode mbox_property_call(volatile uint32_t *buf);

/**
 * @brief Convenience wrapper: GET_CLOCK_RATE for clock_id (an MBOX_CLOCK_ID_* value), in Hz.
 */
StatusCode mbox_get_clock_rate(uint32_t clock_id, uint32_t *out_hz);

/**
 * @brief Convenience wrapper: GET_BOARD_SERIAL, as a 64-bit value split across two 32-bit words.
 */
StatusCode mbox_get_board_serial(uint32_t *out_serial_lo, uint32_t *out_serial_hi);

/**
 * @brief Convenience wrapper: GET_ARM_MEMORY — the ARM-visible RAM base and size, in bytes.
 */
StatusCode mbox_get_arm_memory(uint32_t *out_base, uint32_t *out_size);

/**
 * @brief Bring-up self-test: queries the firmware revision and prints it over UART.
 * @return E_OK if the VC responded to a property-tag call at all.
 */
StatusCode mbox_selftest(void);

#endif
