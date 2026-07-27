# memory_map

Shared RAM address constants used across the bootstrap → bootloader → app
handoff chain. All three images are built and linked separately, so these
addresses are the contract between them.

| Constant | Address | Who loads it |
|---|---|---|
| `BOOTSTRAP_START_ADDR` | 0x8000 | GPU firmware loads `kernel7l.img` (bootstrap) here |
| `BOOTLOADER_START_ADDR` | 0x10000 | bootstrap loads the bootloader here |
| `BOOT_FLAGS_START_ADDR` | 0x88000 | shared RAM between bootloader and app (see `boot_flags`) |
| `APP_START_ADDR` | 0x88400 | bootloader loads the app here |
| `CORE_MAILBOX_ADDR` | 0x88300 | secondary-core entry-point mailbox (see `smp`) |
