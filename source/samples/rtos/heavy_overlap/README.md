# full_demo

Full RTOS demonstration for the Raspberry Pi CM4.

Loads at `0x88400` (after the bootloader hands off control). Exercises the core kernel primitives: preemptive scheduling, semaphores, queues, and the DFU trigger mechanism.

## What it does

| Task | Priority | Behaviour |
|---|---|---|
| `task_1_func` | 5 (highest) | Queue producer — sends an incrementing counter every 100 ms |
| `task_4_func` | 5 | Queue consumer — drains the queue every 1 s |
| `task_2_func` | 3 | Semaphore giver — releases every 1 s |
| `task_3_func` | 4 | Semaphore taker — takes with 100 ms timeout |
| `task_5_func` | 1 (lowest) | Stack monitor — prints TCB stack usage every 200 ms |
| `dfu_trigger_task` | 5 | Watches UART for `DF 00 DF 00` trigger; reboots to bootloader |

## Memory layout

| Region | Address |
|---|---|
| Shared boot flags | `0x88000` |
| Application image | `0x88400` |

## Build

```bash
make SAMPLE=rtos/full_demo
```

Output: `build/rtos/full_demo/full_demo.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`
- **Kernel**: `scheduler`, `task`, `semaphore`, `mutex`, `queue`, `heap`
- **Boot library**: `dfu_trigger`, `boot_flags`
- **Libraries**: `delay`
