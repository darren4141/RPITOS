# telemetry

Optional, compile-time-gated (`RTOS_TELEMETRY`) instrumentation that streams
scheduler activity off-device for a live dashboard. Zero footprint in a
normal build — this whole directory's contents, and every hook site
elsewhere in the kernel, are `#ifdef RTOS_TELEMETRY`.

## Wire framing

```
[0xA5][TYPE:1][SEQ:1][LEN_MSB:1][LEN_LSB:1][PAYLOAD:LEN][CRC32:4 LE][0x5A]
```

`SEQ` wraps at 256, per packet type — lets the host detect drops without any
retransmit (the device streams voluntarily; there's no ACK/NACK). CRC32
covers everything from the magic byte through the payload. See
`md/client/transport_protocol.md` for the full design rationale.

## Packet types

| Type | Payload | Sent by |
|---|---|---|
| `PKT_HEARTBEAT` | 4-byte counter | `telemetry_publisher_task`, ~10 Hz |
| `PKT_TASK_CREATED` | task_id:2 + core_id:1 + priority:1 + name (≤`TELEMETRY_TASK_NAME_MAX` bytes, not NUL-terminated) | `task_create()`, once per task; idle task's setup in `scheduler_init()` |
| `PKT_TICK_STATE` | core_id:1 + task_id:2 + state:1 + overflow_count:1 | `scheduler_switch_context()`, every tick, every core |
| `PKT_TASK_BLOCKED` / `PKT_TASK_UNBLOCKED` | task_id:2 + core_id:1 | `block_until()`/`semaphore_take()`/`mutex_lock()`, and their unblock counterparts |

`PKT_TASK_BLOCKED`/`_UNBLOCKED` carry no "why" field — `TaskWakeupReason` is
a `semaphore.c`-internal signal, not scheduler-wide (`mutex_unlock()` never
sets it), so including it would misreport the mutex path. See
`md/client/device/instrumentation.md`'s "Where to hook" for why exactly
these six call sites, and not e.g. `scheduler_add_to_blocked_list()`.

## `telemetry_lock`

A single `Spinlock` (`spinlock/docs.md`) guards every call into
`telemetry_send_framed()`, including `telemetry_send()` itself — not just
the per-task-event wrappers. Needed because more than one core can write the
wire concurrently (`task_create()`/block/unblock calls fire from whichever
core triggers them); an earlier unlocked `telemetry_send()` let a core-3
heartbeat interleave byte-by-byte with another core's locked send,
corrupting both frames on the wire (caught as a CRC/trailer failure on the
host, which silently drops both). See instrumentation.md's Third postmortem
for the full story, including why a one-shot event like `PKT_TASK_BLOCKED`
doesn't self-heal the way the repeating tick stream does.

Every `telemetry_lock`-touching call site must run with IRQs masked (own
`enter_critical()`, or IRQ context outright) to avoid a same-core deadlock
against its own timer IRQ. `task_create()` is the one exception that needs
its own explicit `enter_critical()` — see `task/docs.md`.

## Per-core tick-state rings

One fixed-depth ring + one `Spinlock` per core (`TELEMETRY_TICK_RING_DEPTH =
8`). Producer: that core's own `scheduler_switch_context()`, via
`telemetry_report_tick_state()` — cheap by design (lock, copy ~5 bytes,
unlock, never touches the UART). Consumer: `telemetry_drain_tick_rings()`,
called from `telemetry_publisher_task`'s loop. On a full ring the producer
drops the new record (not the oldest) and increments that core's
`overflow_count`, which rides along in the next `PKT_TICK_STATE` the
publisher manages to send — a running per-core total the host can surface as
a dropped-record warning, rather than needing a dedicated overflow packet.

## `telemetry_publisher_task`

Runs alone on a dedicated core (core 3 in `multicore_full_demo` — see its
README) so blocking, unbuffered `uart_telemetry_tx_raw()` writes cost it
nothing. Loop: drain every core's tick-ring every iteration (~1 kHz), plus
send one `PKT_HEARTBEAT` every 100th iteration (~10 Hz).

## Bootstrap ordering

`uart_telemetry_init()` (see `uart/docs.md`) must run once, before any
core's `scheduler_init()` — every core's idle-task setup broadcasts
`PKT_TASK_CREATED` during `scheduler_init()`, which hangs spinning on
`FR_TXFF` if it runs against an unconfigured UART.
