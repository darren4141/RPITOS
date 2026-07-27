# multicore_blink

Minimal AMP (asymmetric multiprocessing) demo for the Raspberry Pi CM4.

Core 0 runs the RTOS: it starts the UART task and sends a periodic UART
message from a scheduled task. Core 0 also releases secondary core 1 (via
`smp_start_core`), which runs a bare, kernel-free loop blinking an LED on
GPIO 16.

This proves the AMP bring-up path end to end: secondary release from the
bootstrap parking loop, per-core HYP-exit/cache-disable/stack setup (done in
the bootstrap startup), and a secondary core touching a peripheral
correctly. See [md/amp_multicore_plan.md](../../../../md/amp_multicore_plan.md)
for the broader multicore plan this sample is one step of.

**Scope**: one secondary core, no shared writable state, busy-loop delay on
the secondary (no per-core timer/GIC yet).

## What it does

| Core | Behaviour |
|---|---|
| 0 (RTOS) | `core0_uart_task` — prints a message + tick count every 1 s |
| 1 (bare) | `core1_blink` — toggles GPIO 16 in a busy-wait loop, no scheduler |

## Build

```bash
make SAMPLE=rtos/multicore_blink
```

Output: `build/rtos/multicore_blink/multicore_blink.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`
- **Kernel**: `scheduler`, `task`, `semaphore`, `heap`, `smp`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
