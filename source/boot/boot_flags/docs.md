# boot_flags

`BootFlags` is placed at a fixed address (`BOOT_FLAGS_START_ADDR`, 0x88000)
in RAM shared between the bootloader and the running app — it's how the app
requests a DFU session (`dfu_requested`) and how the bootloader reports back
whether firmware CRC validation passed (`fw_crc_ok`), across a warm jump
that doesn't clear RAM.

`boot_flags_init()` treats a missing/wrong magic word as a cold boot and
resets the struct to safe defaults, since a torn or never-written region
can't be trusted.
