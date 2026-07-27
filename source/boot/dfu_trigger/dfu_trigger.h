#ifndef DFU_TRIGGER_H
#define DFU_TRIGGER_H

#include <stdint.h>

#include "status.h"

// Bytes DF, 00, DF, 00 shifted in MSB-first. Sent raw (not framed in the
// CMD_* packet protocol) so both the app and the bootloader can recognize it
// with this same trivial matcher.
#define DFU_TRIGGER_KEY 0xDF00DF00U

/**
 * @brief Clear the matcher's internal shift register.
 */
void dfu_trigger_reset(void);

/**
 * @brief Return the matcher's current shift-register value.
 */
int dfu_trigger_get_val();

/**
 * @brief Feed one incoming byte to the matcher.
 * @return Non-zero exactly when the last 4 bytes fed in equal DFU_TRIGGER_KEY.
 */
int dfu_trigger_feed(uint8_t byte);

// ── RTOS reboot path (not available in the minimal bootloader build) ──────────
#ifndef UART_MINIMAL

/**
 * @brief ISR-context feed of the trigger matcher, called unconditionally from uart_rx_irq_handler() for every RX byte on every app.
 * @note Not something main() registers — this is what makes DFU recovery work
 * even for an app that never touches the DFU APIs. On a key match it signals
 * the reboot task rather than rebooting from IRQ context, because
 * enter_bootloader() zeros the banked stacks (including the live IRQ stack).
 */
void dfu_trigger_feed_isr(uint8_t byte);

/**
 * @brief Create the semaphore-blocked task that performs the reboot in task context once the trigger fires.
 * @note Called unconditionally from scheduler_init() (scheduler.c) — do NOT
 * call this from main() as well, it would create a second, orphaned reboot task.
 */
StatusCode dfu_trigger_task_start(void);
#endif

#endif
