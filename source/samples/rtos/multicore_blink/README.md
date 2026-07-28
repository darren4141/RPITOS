# multicore_blink

BMP (per-core scheduler) demo for the Raspberry Pi CM4 — each core runs its
own independent RTOS scheduler instance, not one shared ready list.

Core 0 runs the RTOS: it starts the UART task and sends a periodic UART
message from a scheduled task. Core 0 also releases secondary core 1 (via
`smp_start_core`), which now boots **its own scheduler** (`core1_kmain` →
`gic_percore_init()` → `gentimer_init()` → `scheduler_init(1, ...)` →
`task_create()` → `scheduler_start()`) and runs a real scheduled task
(`core1_blink_task`) that blinks GPIO 16 via `task_delay_ms()`, not a busy
loop. Cores 2/3 remain bare, kernel-free loops for now.

This is a step beyond the original AMP milestone (core 1 as a bare loop): it
proves a secondary core can take real IRQ-driven interrupts (its own timer
tick, its own context switches) rather than just touching a peripheral once.
See [md/amp_multicore_plan.md](../../../../md/amp_multicore_plan.md) for the
broader multicore plan and current milestone status.

**Scope**: core 0 + core 1 both run full independent schedulers; cores 2/3
stay bare loops. Cross-core synchronization primitives (`semaphore_give()`,
`mutex_unlock()`) are safe to call between core 0 and core 1's tasks — see
`spinlock/docs.md` and `scheduler.h`'s `scheduler_lock()`.

## What it does

| Core | Behaviour |
|---|---|
| 0 (RTOS) | `core0_uart_task` — prints a message + tick count every 1 s |
| 1 (RTOS) | `core1_blink_task` — its own scheduler; toggles GPIO 16 via `task_delay_ms()` |
| 2/3 (bare) | busy-wait loops, no scheduler |

## Build

```bash
make SAMPLE=rtos/multicore_blink
```

Output: `build/rtos/multicore_blink/multicore_blink.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`
- **Kernel**: `scheduler`, `task`, `semaphore`, `heap`, `smp`, `spinlock`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
