# multicore_full_demo

Four-core demo of the RTOS synchronization primitives genuinely working
*across* cores, not just within one. Every core (0-3) runs its own
independent scheduler instance (`companion_core_start()` — see
`companion_core/docs.md`), and all three primitives are shared between tasks
that live on different cores' schedulers:

- **Mutex** (`g_counter_mutex` / `g_shared_counter`) — one task on **every**
  core (`mutex_counter_task`) locks the same mutex, increments the same
  counter, prints it, unlocks, and delays. Four-way cross-core contention on
  one resource — the printed sequence should show every value exactly once,
  never skipped or duplicated, regardless of which core produced it.
- **Semaphore** (`g_ping_sem`) — core 0's `ping_task` gives it every 500 ms;
  core 1's `pong_task` blocks on `semaphore_take()` and wakes the instant it
  does. Demonstrates a give on one core correctly waking a task parked on a
  *different* core's scheduler.
- **Queue** (`g_msg_queue`) — core 2's `queue_send_task` sends an
  incrementing message every 400 ms; core 3's `queue_recv_task` blocks on
  `queue_recv()` and wakes the instant one arrives. Same cross-core wake
  property as the semaphore, plus the actual data copy through the shared
  buffer.

## What it does

| Core | Behaviour |
|---|---|
| 0 | `mutex_counter_task` (contends the shared mutex) + `ping_task` (gives the semaphore every 500 ms) |
| 1 | `mutex_counter_task` + `pong_task` (blocks on the semaphore) |
| 2 | `mutex_counter_task` + `queue_send_task` (sends every 400 ms) |
| 3 | `mutex_counter_task` + `queue_recv_task` (blocks on the queue) |

All four cores also print via the single shared UART — see `uart/docs.md`'s
Multicore section for why that's safe with producers on every core at once.

## Build

```bash
make SAMPLE=rtos/multicore_full_demo
```

Output: `build/rtos/multicore_full_demo/multicore_full_demo.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`
- **Kernel**: `scheduler`, `task`, `semaphore`, `mutex`, `queue`, `heap`, `companion_core`, `spinlock`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
