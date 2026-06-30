#ifndef BOOT_FLAGS_H
#define BOOT_FLAGS_H

#include <stdint.h>

#define BOOT_FLAGS_MAGIC 0xB007F1A6U
#define DFU_REQUEST      0xB007AB1E

typedef enum {
  RESET_REASON_COLD     = 0,
  RESET_REASON_WATCHDOG = 1,
  RESET_REASON_SOFTWARE = 2,
} ResetReason;

typedef struct {
  uint32_t magic;               // BOOT_FLAGS_MAGIC when valid
  uint32_t reset_reason;        // ResetReason
  uint32_t dfu_requested;       // non-zero → enter DFU on next boot
  uint32_t fw_crc_ok;           // non-zero → bootloader verified firmware CRC
  uint32_t wdt_reset_count;     // incremented by bootloader on each WDT reset; cleared on app launch
  int32_t  wdt_reset_tolerance; // set by app via watchdog_init; -1 = infinite (never fire policy)
  uint32_t wdt_reset_policy;    // WatchdogResetPolicy; what to do when count exceeds tolerance
  uint32_t wdt_reset_reason;    // WatchdogResetReason; reserved for future diagnostics
} BootFlags;

// Placed at 0x88000 - shared physical RAM
extern volatile BootFlags boot_flags;

// Called by bootloader: sets magic
void boot_flags_init(void);

// Called by kernel: returns non-zero if bootloader populated the struct
int boot_flags_valid(void);

#endif
