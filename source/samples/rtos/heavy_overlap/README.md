# heavy_overlap

Same core-0 RTOS workload as `full_demo` (queue, semaphore, stack monitor),
run concurrently with an unmanaged, un-synchronized bare loop hammering GPIO
on core 1 — no scheduler, no yields, just `companion_core_start()` releasing
core 1 straight into `core1_blink`. Exercises core 0's full task set staying
correct and its UART output staying clean while core 1 is maximally busy
with nothing coordinating it.

## What it does

| Task | Priority | Behaviour |
|---|---|---|
| `task_1_func` | 5 (highest) | Queue producer — sends an incrementing counter every 100 ms |
| `task_4_func` | 5 | Queue consumer — drains the queue every 1 s |
| `task_2_func` | 3 | Semaphore giver — releases every 1 s |
| `task_3_func` | 4 | Semaphore taker — takes with 100 ms timeout |
| `task_5_func` | 1 (lowest) | Stack monitor — prints TCB stack usage every 200 ms |
| `dfu_trigger_task` | 5 | Watches UART for `DF 00 DF 00` trigger; reboots to bootloader |
| `core1_blink` (core 1, bare) | n/a | No scheduler — tight loop toggling GPIO 16 on/off with busy-wait delays |

## Build

```bash
make SAMPLE=rtos/heavy_overlap
```

Output: `build/rtos/heavy_overlap/heavy_overlap.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `watchdog`
- **Kernel**: `scheduler`, `task`, `semaphore`, `mutex`, `queue`, `heap`, `companion_core`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
- **Libraries**: `delay`
