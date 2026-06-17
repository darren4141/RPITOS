#include "boot_flags.h"

volatile BootFlags boot_flags __attribute__((section(".shared_flags")));

void boot_flags_init(void)
{
  if (!boot_flags_valid()) {
    // Magic word didn't survive — either a true power-on (RAM is volatile,
    // guaranteed not to retain anything) or a reset we can't be certain
    // preserved RAM (see bootloader_init() in boot/app/main.c for why even
    // our own watchdog-triggered system_reset() isn't a sure thing). Either
    // way, nothing else in this struct can be trusted either — reset to
    // safe defaults. The caller (bootloader_init()) re-derives fw_crc_ok
    // from scratch by checking eMMC directly.
    boot_flags.reset_reason = RESET_REASON_COLD;
    boot_flags.dfu_requested = 0;
    boot_flags.fw_crc_ok = 0;
    boot_flags.magic = BOOT_FLAGS_MAGIC;
  }
  // else: magic survived, so we trust the rest of the struct survived with
  // it (single contiguous region) — leave dfu_requested/reset_reason/
  // fw_crc_ok exactly as the previous boot left them.
}

int boot_flags_valid(void)
{
  return boot_flags.magic == BOOT_FLAGS_MAGIC;
}
