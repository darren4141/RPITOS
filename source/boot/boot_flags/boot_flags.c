#include "boot_flags.h"

volatile BootFlags boot_flags __attribute__((section(".shared_flags")));

void boot_flags_init(void)
{
  if (!boot_flags_valid()) {
    // Magic word didn't survive -> Cold boot: we cannot trust anything in this struct
    boot_flags.reset_reason = RESET_REASON_COLD;
    boot_flags.dfu_requested = 0U;
    boot_flags.fw_crc_ok = 0U;
    boot_flags.magic = BOOT_FLAGS_MAGIC;
  }
  else {
    // magic survived, so we trust the rest of the struct survived with
  }
}

int boot_flags_valid(void)
{
  return boot_flags.magic == BOOT_FLAGS_MAGIC;
}
