#include "boot_flags.h"

volatile BootFlags boot_flags __attribute__((section(".shared_flags")));

void boot_flags_init(void)
{
  boot_flags.magic = BOOT_FLAGS_MAGIC;
}

int boot_flags_valid(void)
{
  return boot_flags.magic == BOOT_FLAGS_MAGIC;
}
