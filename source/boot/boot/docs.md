# boot

Validates and loads an app image from eMMC into RAM, then jumps to it. Used
by the bootloader sample after it has picked an active app slot (see
`emmc/docs.md` for the slot layout).

`boot_validate_app()` and `boot_load_app()` both operate on `current_sector`,
a single static sector-sized scratch buffer — the component is not
reentrant and expects to be driven from one task.
