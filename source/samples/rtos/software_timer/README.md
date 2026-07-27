# software_timer

Software timer demo for the Raspberry Pi CM4.

Exercises the software-timer subsystem on top of the scheduler tick. Five
statically-allocated timers are armed from a control task:

| Timer | Period | Mode | Behaviour |
|---|---|---|---|
| heartbeat | 250 ms | periodic | toggles the LED on GPIO 16 |
| fast | 500 ms | periodic | prints |
| medium | 1 s | periodic | prints |
| slow | 2 s | periodic | prints |
| one-shot | 5 s | one-shot | prints once, then never again |

At 8 s the control task stops the fast timer and resets the slow timer, so
the effect of `software_timer_stop()`/`software_timer_reset()` is visible in
the log. Callbacks run in the software-timer service task (`TASK_PRIORITY_5`).

A second task (`spawner_task`) continuously creates one-shot timers from a
fixed 8-slot pool, cycling through periods 200–799 ms, to exercise timer
creation/expiry under a steadier load than the five fixed timers alone.

Only the uart, dfu-trigger, and watchdog tasks are pulled in besides the
timer subsystem itself.

## Build

```bash
make SAMPLE=rtos/software_timer
```

Output: `build/rtos/software_timer/software_timer.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `watchdog`, `emmc`, `crc`, `dma`
- **Kernel**: `scheduler`, `task`, `semaphore`, `heap`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
