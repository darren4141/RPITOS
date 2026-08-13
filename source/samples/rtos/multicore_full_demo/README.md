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
  incrementing message every 150 ms; core 0's `queue_recv_task` blocks on
  `queue_recv()` and wakes the instant one arrives. Same cross-core wake
  property as the semaphore, plus the actual data copy through the shared
  buffer.
- **Handshake** (`g_data_ready_sem` / `g_processing_done_sem`) — a
  round-trip producer/consumer between core 1 (`handshake_producer_task`)
  and core 2 (`handshake_consumer_task`), unlike `g_ping_sem`'s
  one-directional fire-and-forget. Core 1 "produces" (busy work, ~700 ms),
  gives `g_data_ready_sem`, then blocks on `g_processing_done_sem`; core 2
  blocks on `g_data_ready_sem`, "processes" (busy work, ~1800 ms —
  deliberately longer than the producer's), then gives the ack back. Strict
  alternation: while one side works, the other is always blocked.
- **PCA9685 blink** (`pca_blink_task`, core 0) — drives a PCA9685 PWM/LED
  controller over `I2C_CHANNEL_3` (GPIO4/5, ALT5), toggling its PWM output
  channels 4 and 5 together between full-on and full-off at 1 Hz. Not part of
  the cross-core synchronization story the rest of this sample demonstrates —
  it's here as a real-hardware smoke test for the `i2c`/`pwm_pca9685` drivers
  under the same multicore/scheduler load as everything else. `i2c_channel_init()`
  runs in `kmain()` (no task-context requirement); `pwm_pca9685_init()` runs
  at the top of `pca_blink_task()` itself, since it blocks on `task_delay_ms()`
  for the datasheet's post-SLEEP-clear settle time and so needs to run as a
  task, not from `kmain()` before `scheduler_start()` — see `pwm_pca9685/docs.md`.
- **Grind** (`grind_task`, one per app core 0-2) — a CPU-bound filler that
  busy-spins ~50 ms then delays 15 ms, keeping each core genuinely busy
  between the lighter-weight demo tasks' delays so contention/idle-time
  shows up on the telemetry dashboard instead of everything finishing
  near-instantly. Runs at the lowest app priority so it never delays
  ping/pong/queue/handshake traffic.
- **Telemetry** (core 3, `RTOS_TELEMETRY`) — `telemetry_publisher_task` is
  the *only* task on this core. It frames and sends a `PKT_HEARTBEAT` packet
  at 10 Hz using the wire format from `md/client/transport_protocol.md`
  (magic/type/seq/len/payload/CRC32/trailer) — the trivial packet type
  Phase 1 of that plan calls for, to validate framing, CRC, and sequence
  numbering before any real scheduler data rides on it. Goes out over a
  **dedicated UART5 instance (GPIO12, 921600 baud, TX-only, raw blocking
  writes)** — a completely separate wire from UART0/the console, so it's
  never interleaved with `uart_print`/`uart_printf` debug output from
  cores 0-2. See `uart_channel_init()`/`uart_channel_tx_raw()` (channel
  `UART_CHANNEL_TELEMETRY`) in the `uart` driver.

## What it does

| Core | Behaviour |
|---|---|
| 0 | `mutex_counter_task` + `ping_task` (gives the semaphore every 500 ms) + `queue_recv_task` (blocks on the queue) + `grind_task` + `pca_blink_task` (PCA9685 channels 4/5, 1 Hz) |
| 1 | `mutex_counter_task` + `pong_task` (blocks on the semaphore) + `handshake_producer_task` + `grind_task` |
| 2 | `mutex_counter_task` + `queue_send_task` (sends every 150 ms) + `handshake_consumer_task` + `grind_task` |
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

- **Drivers**: `gpio`, `uart`, `gentimer`, `gic`, `jtag`, `reset`, `interrupts`, `dma`, `watchdog`, `emmc`, `crc`, `i2c`, `mailbox`, `pwm_pca9685`
- **Kernel**: `scheduler`, `task`, `semaphore`, `mutex`, `queue`, `heap`, `companion_core`, `spinlock`, `software_timer`
- **Boot library**: `dfu_trigger`, `boot_flags`
- **Telemetry**: `telemetry` (gated by `-DRTOS_TELEMETRY`, see `config.mk`)
