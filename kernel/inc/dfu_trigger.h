#ifndef DFU_TRIGGER_H
#define DFU_TRIGGER_H

#include <stdint.h>

// Bytes DF, 00, DF, 00 shifted in MSB-first. Sent raw (not framed in the
// CMD_* packet protocol) so both the app and the bootloader can recognize it
// with this same trivial matcher.
#define DFU_TRIGGER_KEY 0xDF00DF00U

// Set to 1 by timer_tick_handler (IRQ context) when the trigger key is matched.
// Cleared by dfu_trigger_task after it handles the reboot.
extern volatile uint32_t dfu_pending;

// Clears the matcher's internal shift register.
void dfu_trigger_reset(void);

int dfu_trigger_get_val();

// Feed one incoming byte to the matcher. Returns non-zero exactly when the
// last 4 bytes fed in equal DFU_TRIGGER_KEY.
int dfu_trigger_feed(uint8_t byte);

#endif
