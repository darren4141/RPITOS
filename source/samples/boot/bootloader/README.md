# bootloader

Bare-metal DFU bootloader for the Raspberry Pi CM4.

Loads at `0x8000` (RPi AArch32 firmware entry point). Validates the application image stored in eMMC, then jumps to it. Enters DFU receive mode if the image is invalid, a DFU trigger key is sent over UART during the recovery window, or the `dfu_requested` boot flag is set.

## Memory layout

| Region | Address | Size |
|---|---|---|
| Bootloader image | `0x8000` | up to 512 KB |
| Shared boot flags | `0x88000` | 1 KB |
| Application image (RAM) | `0x88400` | — |

## Build

```bash
make SAMPLE=boot/bootloader
```

Output: `build/boot/bootloader/bootloader.elf / .img / .hex`

## DFU trigger

Send the 4-byte sequence `DF 00 DF 00` over UART at any time to request a DFU session. The bootloader detects this during the 100 ms recovery window on startup, or the running kernel's `dfu_trigger_task` detects it and sets `boot_flags.dfu_requested` before rebooting.

## Components used

- **Drivers**: `gpio`, `uart`, `emmc`, `crc`, `jtag`
- **Boot library**: `boot`, `dfu_receive`, `dfu_trigger`, `boot_flags`
