# mutex_inheritance

Priority inheritance demo for the Raspberry Pi CM4.

Three tasks contend on a single inheritance-enabled mutex:

| Task | Priority | Behaviour |
|---|---|---|
| LOW | 1 | Acquires the mutex and busy-spins for 2000 ms |
| MID | 2 | Never touches the mutex; prints a tick every 50 ms |
| HIGH | 3 | Sleeps 100 ms, then blocks on the same mutex |

## Why busy-spin and not `task_delay_ms` while holding the mutex?

`task_delay_ms` removes the task from the ready list (`TASK_STATE_BLOCKED`).
A blocked task has no priority as far as the scheduler is concerned, so
boosting it does nothing — MID would run freely regardless. Busy-spinning
keeps LOW in the READY/RUNNING state so the scheduler compares priorities
every tick.

## Expected output with inheritance ON

```
[    0] LOW : acquired (priority=1 base=1) — starting 2000 ms work
[    0] MID : tick 1   <-- MID runs; LOW is only priority 1
[   50] MID : tick 2
[  100] HIGH: blocking on mutex — LOW should be boosted to 3 now
[  100] LOW : working... (priority=3 base=1)  <-- boost visible here
         *** MID goes silent — can't preempt LOW at priority 3 ***
[  150] LOW : working... (priority=3 base=1)
[  200] LOW : working... (priority=3 base=1)
...
[ 1950] LOW : working... (priority=3 base=1)
[ 2000] LOW : releasing  (priority=3 base=1)
[ 2000] HIGH: acquired mutex ✓
[ 2000] LOW : released   (priority=1 base=1)  <-- restored
[ 2000] MID : tick ...   <-- MID resumes
...
```

Without inheritance LOW stays at priority 1, MID (priority 2) preempts it
every tick, and MID prints continuously even while HIGH is stuck waiting.
Toggle `mutex_set_inheritance(&shared_mtx, 0)` in `kmain` to observe this.

## Build

```bash
make SAMPLE=rtos/mutex_inheritance
```

Output: `build/rtos/mutex_inheritance/mutex_inheritance.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `watchdog`, `emmc`, `crc`, `dma`
- **Kernel**: `scheduler`, `task`, `mutex`, `semaphore`, `heap`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
