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

## Sync primitive tracking

Each mutex/semaphore/queue gets a `sync_id` at init time
(`telemetry_register_sync()`), which broadcasts `PKT_SYNC_CREATED` once and
remembers the registration (`TELEMETRY_MAX_SYNC_OBJECTS = 16`, write-once
entries; `multicore_full_demo` registers 12 today) for later
re-announcement. All kinds share one id counter, allocated+broadcast
atomically under `telemetry_lock`. `PKT_MUTEX_OWNER_CHANGED` and the
`sync_kind`/`sync_id` fields on `PKT_TASK_BLOCKED` ride on the same ids.
See `md/client/device/sync_view.md` for the full design.

**Why the roster is re-broadcast, not sent once:** `PKT_SYNC_CREATED` fires
once, early in `kmain()`, squarely inside the power-up/reset line-noise
window — losing that one frame to a CRC failure meant the object never
appeared on the host, unlike the self-healing `PKT_TICK_STATE`/
`PKT_HEARTBEAT` streams. `telemetry_rebroadcast_sync_roster()` re-sends the
whole roster every ~2s from `telemetry_publisher_task`; idempotent on the
host side.

**Why each per-entry send in the rebroadcast masks IRQs
(`enter_critical()`/`exit_critical()`):** `Spinlock` only protects against
other *cores*, not a same-core IRQ re-entering a lock the interrupted code
already holds. The multi-frame rebroadcast burst is long enough to reliably
straddle `telemetry_publisher_task`'s own ~1kHz timer tick, whose handler
also calls into `telemetry_lock` — sending unmasked let that IRQ splice its
bytes into an in-flight frame, corrupting it beyond even a CRC-detectable
failure. Full root-cause trace in `sync_view.md`.

**Why the rebroadcast loop drains the tick rings instead of sleeping
between frames:** an inter-frame `task_delay_ms(1)` starved this loop's
caller of its own `telemetry_drain_tick_rings()` calls for ~12ms per
rebroadcast — long enough to overflow every core's tick ring (depth 8 at
~1kHz production). Calling `telemetry_drain_tick_rings()` inline keeps the
same "leave a gap between frames" intent without starving the rings.

## Boot / DFU tracking

Reports boot-stage milestones (`PKT_BOOT_STAGE_ENTER`/`PKT_BOOT_MILESTONE`),
struct snapshots (`PKT_BOOT_INFO`, discriminated by
`TelemetryBootInfoSubtype`), and DFU protocol events (`PKT_DFU_EVENT`). See
`md/client/device/boot_init_tracking.md` for the full design, milestone
list, and packet layouts.

Two independent senders share the wire framer (`telemetry_frame.c`'s
`telemetry_send_framed()`, extracted from this file for this purpose) but
not the locking: `telemetry_boot.c` (bootstrap/bootloader — `UART_MINIMAL`,
no RTOS, unlocked because both images are single-threaded) and this file's
own locked wrappers (app side — defined here for API symmetry with
`telemetry_boot.c`, even though the app only calls the
stage/milestone/slot-confirmed subset today). Exactly one of the two links
into any given sample (Makefile's `SAMPLE_TELEMETRY`/
`SAMPLE_TELEMETRY_BOOT`), so the shared function names never collide.
