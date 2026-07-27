# bootstrap

Stage-0 loader for the Raspberry Pi CM4. This is the actual `kernel7l.img`
the GPU firmware loads directly — it exists only to chainload the real
bootloader out of eMMC into RAM, so the bootloader itself can be updated via
DFU without ever touching the SD card.

Loads at `0x8000` (RPi AArch32 firmware entry point, same as the
bootloader). Reads the bootloader's header + binary from
`EMMC_SECTOR_BOOTLOADER`, validates its CRC32, then jumps to it at
`BOOTLOADER_LOAD_ADDR` (`0x10000`). Hangs (prints an error, loops forever)
on any eMMC or CRC failure — there is no recovery path at this stage; that's
what the bootloader's own DFU mode is for.

Built with `UART_MINIMAL` (blocking TX, no scheduler) since there's no RTOS
at this stage — see [uart/docs.md](../../../drivers/uart/docs.md).

## Build

```bash
make SAMPLE=boot/bootstrap
```

Output: `build/boot/bootstrap/bootstrap.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `emmc`, `crc`
