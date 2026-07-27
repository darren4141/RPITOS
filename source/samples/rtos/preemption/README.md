# preemption

Plain priority-preemption demo for the Raspberry Pi CM4 — the baseline
`mutex_inheritance` compares against, minus the mutex.

Three tasks at different priorities, none of them touching a mutex:

| Task | Priority | Behaviour |
|---|---|---|
| LOW | 1 | Busy-spins for 300 ticks printing a counter, then sleeps 10 ms, repeats |
| MID | 2 | Prints a tick every 50 ms |
| HIGH | 3 | Prints every 1300 ms |

Because nothing here holds a mutex or triggers priority inheritance, MID and
HIGH preempt LOW's busy loop on every tick as ordinary priority scheduling
would predict — contrast with `mutex_inheritance`, where MID goes silent
while LOW is boosted to HIGH's priority during the mutex hold.

## Build

```bash
make SAMPLE=rtos/preemption
```

Output: `build/rtos/preemption/preemption.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `watchdog`, `emmc`, `crc`, `dma`
- **Kernel**: `scheduler`, `task`, `semaphore`, `heap`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
