# wiper

Recovery utility for the Raspberry Pi CM4 — flashed in place of the
bootloader (it reuses the bootloader's `startup.s` and load address,
`0x10000`, via `SAMPLE_STARTUP_OVERRIDE`) to force a device with a broken
or unwanted app back into DFU mode.

Zeroes both app slot headers (`EMMC_SECTOR_APP_A`, `EMMC_SECTOR_APP_B`) and
the metadata sector, and clears the `boot_flags` magic word at `0x88000` so
the real bootloader treats the next boot as cold and finds no valid app in
either A/B slot. Then loads and jumps to the real bootloader from
`EMMC_SECTOR_BOOTLOADER`, same as `bootstrap` does, so the chain continues
normally — just with both app slots invalidated, forcing the bootloader
into its DFU receive loop.

Built with `UART_MINIMAL`, same as `bootstrap`.

## Build

```bash
make SAMPLE=boot/wiper
```

Output: `build/boot/wiper/wiper.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `emmc`, `crc`
