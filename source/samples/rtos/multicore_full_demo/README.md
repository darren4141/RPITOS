# multicore_full_demo

Demo of the RTOS synchronization primitives genuinely working *across* cores,
not just within one, plus a dedicated telemetry-publisher core. Cores 0-3
each run their own independent scheduler instance (`companion_core_start()`
— see `companion_core/docs.md`). Cores 0-2 run the RTOS-under-test demo;
core 3 is carved out entirely for telemetry (see `md/client/` and
`telemetry/telemetry.h`) so it can never be starved by, or itself starve,
the mutex/semaphore/queue workload:

- **Mutex** (`g_counter_mutex` / `g_shared_counter`) — one task on cores
  0-2 (`mutex_counter_task`) locks the same mutex, increments the same
  counter, prints it, unlocks, and delays. Three-way cross-core contention
  on one resource — the printed sequence should show every value exactly
  once, never skipped or duplicated, regardless of which core produced it.
- **Semaphore** (`g_ping_sem`) — core 0's `ping_task` gives it every 500 ms;
  core 1's `pong_task` blocks on `semaphore_take()` and wakes the instant it
  does. Demonstrates a give on one core correctly waking a task parked on a
  *different* core's scheduler.
- **Queue** (`g_msg_queue`) — core 2's `queue_send_task` sends an
  incrementing message every 400 ms; core 0's `queue_recv_task` blocks on
  `queue_recv()` and wakes the instant one arrives. Same cross-core wake
  property as the semaphore, plus the actual data copy through the shared
  buffer.
- **Telemetry** (core 3, `RTOS_TELEMETRY`) — `telemetry_publisher_task` is
  the *only* task on this core. It frames and sends a `PKT_HEARTBEAT` packet
  at 10 Hz using the wire format from `md/client/transport_protocol.md`
  (magic/type/seq/len/payload/CRC32/trailer) — the trivial packet type
  Phase 1 of that plan calls for, to validate framing, CRC, and sequence
  numbering before any real scheduler data rides on it. Goes out over a
  **dedicated UART3 instance (GPIO4, 921600 baud, TX-only, raw blocking
  writes)** — a completely separate wire from UART0/the console, so it's
  never interleaved with `uart_print`/`uart_printf` debug output from
  cores 0-2. See `uart_telemetry_init()`/`uart_telemetry_tx_raw()` in the
  `uart` driver.

## What it does

| Core | Behaviour |
|---|---|
| 0 | `mutex_counter_task` + `ping_task` (gives the semaphore every 500 ms) + `queue_recv_task` (blocks on the queue) |
| 1 | `mutex_counter_task` + `pong_task` (blocks on the semaphore) |
| 2 | `mutex_counter_task` + `queue_send_task` (sends every 400 ms) |
| 3 | `telemetry_publisher_task` only — dedicated, no RTOS-under-test tasks |

All four cores also print via the single shared UART — see `uart/docs.md`'s
Multicore section for why that's safe with producers on every core at once
(the telemetry publisher shares the same ring-buffer TX path).

## Build

```bash
make SAMPLE=rtos/multicore_full_demo
```

Output: `build/rtos/multicore_full_demo/multicore_full_demo.elf / .img / .hex`

## Components used

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`
- **Kernel**: `scheduler`, `task`, `semaphore`, `mutex`, `queue`, `heap`, `companion_core`, `spinlock`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
- **Telemetry**: `telemetry` (gated by `-DRTOS_TELEMETRY`, see `config.mk`)
