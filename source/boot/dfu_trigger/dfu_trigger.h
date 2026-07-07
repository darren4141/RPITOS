#ifndef DFU_TRIGGER_H
#define DFU_TRIGGER_H

#include <stdint.h>

#include "status.h"

// Bytes DF, 00, DF, 00 shifted in MSB-first. Sent raw (not framed in the
// CMD_* packet protocol) so both the app and the bootloader can recognize it
// with this same trivial matcher.
#define DFU_TRIGGER_KEY 0xDF00DF00U

// Clears the matcher's internal shift register.
void dfu_trigger_reset(void);

int dfu_trigger_get_val();

// Feed one incoming byte to the matcher. Returns non-zero exactly when the
// last 4 bytes fed in equal DFU_TRIGGER_KEY.
int dfu_trigger_feed(uint8_t byte);

// ── RTOS reboot path (not available in the minimal bootloader build) ──────────
#ifndef UART_MINIMAL
// UartRxHandler-shaped: register with uart_rx_irq_enable(). On a key match it
// signals the reboot task — it does NOT reboot from IRQ context, because
// enter_bootloader() zeros the banked stacks (including the live IRQ stack).
void dfu_trigger_feed_isr(uint8_t byte);

// Create the semaphore-blocked task that performs the reboot in task context.
// The task consumes no CPU until the trigger fires. Call before enabling RX IRQ.
StatusCode dfu_trigger_task_start(void);
#endif

#endif
