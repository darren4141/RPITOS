#include "telemetry.h"

#ifdef RTOS_TELEMETRY

#include "companion_core.h"
#include "crc.h"
#include "scheduler.h"
#include "spinlock.h"
#include "uart.h"

static uint8_t seq_counters[NUM_TELEMETRY_PACKET_TYPES] = { 0 };

// Guards every packet type with genuinely concurrent multi-core callers:
// telemetry_report_task_created(), telemetry_report_task_blocked(), and
// telemetry_report_task_unblocked() — any of these can fire from whichever
// core creates/blocks/unblocks a task, including cross-core wakes
// (semaphore_give()/mutex_unlock() targeting a different core's task). Every
// other packet type (PKT_HEARTBEAT, PKT_TICK_STATE) is sent exclusively by
// the dedicated core-3 publisher — single-writer by design, so telemetry_send()
// itself stays lock-free and those calls pay zero locking overhead, including
// PKT_TICK_STATE at ~1 kHz. Zero-initialized by BSS, which is already this
// lock's unlocked state (see spinlock_init()) — no explicit init call needed.
static Spinlock telemetry_lock;

// Actual framing + CRC + UART write. No locking — callers decide whether
// they need it. telemetry_send() (below) calls this directly, trusting the
// caller to be the single dedicated telemetry core; telemetry_report_task_created()
// wraps this in telemetry_lock instead, since task_create()/scheduler_init()
// call it from whichever core creates a task.
static void telemetry_send_framed(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
{
  uint8_t header[5];
  header[0] = TELEMETRY_MAGIC;
  header[1] = (uint8_t)type;
  header[2] = seq_counters[type]++;
  header[3] = 0U;    // LEN_MSB — len is a uint8_t, so this byte is always 0
  header[4] = len;   // LEN_LSB

  CRC32 ctx;
  crc32_start(&ctx);
  crc32_update(&ctx, header, sizeof(header));
  if (len > 0) {
    crc32_update(&ctx, payload, len);
  }
  uint32_t crc = crc32_finish(&ctx);

  for (uint8_t i = 0; i < sizeof(header); i++) {
    uart_telemetry_tx_raw(header[i]);
  }
  for (uint8_t i = 0; i < len; i++) {
    uart_telemetry_tx_raw(payload[i]);
  }

  uart_telemetry_tx_raw((uint8_t)(crc & 0xFFU));           // CRC32 LE
  uart_telemetry_tx_raw((uint8_t)((crc >> 8) & 0xFFU));
  uart_telemetry_tx_raw((uint8_t)((crc >> 16) & 0xFFU));
  uart_telemetry_tx_raw((uint8_t)((crc >> 24) & 0xFFU));

  uart_telemetry_tx_raw(TELEMETRY_TRAILER);
}

void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
{
  if (type >= NUM_TELEMETRY_PACKET_TYPES) {
    return;
  }

  telemetry_send_framed(type, payload, len);
}

void telemetry_report_task_created(uint16_t task_id, uint32_t core_id, uint8_t priority, const char *name)
{
  uint8_t payload[4 + TELEMETRY_TASK_NAME_MAX];

  payload[0] = (uint8_t)(task_id >> 8);
  payload[1] = (uint8_t)(task_id & 0xFFU);
  payload[2] = (uint8_t)core_id;
  payload[3] = priority;

  uint8_t name_len = 0;
  while ((name != NULL) && (name[name_len] != '\0') && (name_len < TELEMETRY_TASK_NAME_MAX)) {
    payload[4 + name_len] = (uint8_t)name[name_len];
    name_len++;
  }

  // Locked: this is the one call path with genuinely concurrent multi-core
  // callers (any core, whenever it creates a task) — see telemetry_lock's
  // doc comment above.
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_TASK_CREATED, payload, (uint8_t)(4 + name_len));
  spinlock_release(&telemetry_lock);
}

// Shared by telemetry_report_task_blocked()/telemetry_report_task_unblocked()
// — same tiny payload shape (task_id + core_id), just a different packet type.
static void telemetry_report_task_event(TelemetryPacketType type, uint16_t task_id, uint32_t core_id)
{
  uint8_t payload[3];
  payload[0] = (uint8_t)(task_id >> 8);
  payload[1] = (uint8_t)(task_id & 0xFFU);
  payload[2] = (uint8_t)core_id;

  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(type, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_task_blocked(uint16_t task_id, uint32_t core_id)
{
  telemetry_report_task_event(PKT_TASK_BLOCKED, task_id, core_id);
}

void telemetry_report_task_unblocked(uint16_t task_id, uint32_t core_id)
{
  telemetry_report_task_event(PKT_TASK_UNBLOCKED, task_id, core_id);
}

// ── Per-core tick-state rings ──────────────────────────────────────────────
// One ring + one Spinlock per core. Producer: that core's own
// scheduler_switch_context() (telemetry_report_tick_state()). Consumer: core
// 3's poll loop (telemetry_drain_tick_rings()), the only thing that ever
// reads across cores here. Each ring's lock is only ever contended between
// exactly those two parties — never between two app cores — so this stays
// cheap even though it's not lock-free. See instrumentation.md's Phase 3
// design note for why a Spinlock was chosen over hand-rolling a lock-free
// SPSC ring.

typedef struct {
  uint8_t core_id;
  uint16_t task_id;
  uint8_t state;
} TelemetryTickRecord;

typedef struct {
  TelemetryTickRecord records[TELEMETRY_TICK_RING_DEPTH];
  uint8_t head;             // next write slot
  uint8_t tail;             // next read slot
  uint8_t count;            // valid entries currently buffered
  uint8_t overflow_count;   // records dropped because the ring was full — wraps; a coarse "something's wrong" signal, not a precise lifetime total
  Spinlock lock;
} TelemetryTickRing;

// Zero-initialized by BSS — a fresh ring (head=tail=count=0) and an unlocked
// Spinlock are both already the correct starting state, same reasoning as
// telemetry_lock above.
static TelemetryTickRing g_tick_rings[COMPANION_CORE_MAX_CORES];

void telemetry_report_tick_state(uint32_t core_id, uint16_t task_id, uint8_t state)
{
  if (core_id >= COMPANION_CORE_MAX_CORES) {
    return;
  }

  TelemetryTickRing *ring = &g_tick_rings[core_id];

  spinlock_acquire(&ring->lock);

  if (ring->count == TELEMETRY_TICK_RING_DEPTH) {
    ring->overflow_count++;   // drop the new record, not the oldest — see telemetry.h's doc comment
  }
  else {
    ring->records[ring->head].core_id = (uint8_t)core_id;
    ring->records[ring->head].task_id = task_id;
    ring->records[ring->head].state = state;
    ring->head = (uint8_t)((ring->head + 1U) % TELEMETRY_TICK_RING_DEPTH);
    ring->count++;
  }

  spinlock_release(&ring->lock);
}

void telemetry_drain_tick_rings(void)
{
  for (uint32_t core_id = 0; core_id < COMPANION_CORE_MAX_CORES; core_id++) {
    TelemetryTickRing *ring = &g_tick_rings[core_id];

    while (1) {
      TelemetryTickRecord record;
      uint8_t overflow;
      int has_record = 0;

      spinlock_acquire(&ring->lock);
      if (ring->count > 0) {
        record = ring->records[ring->tail];
        ring->tail = (uint8_t)((ring->tail + 1U) % TELEMETRY_TICK_RING_DEPTH);
        ring->count--;
        has_record = 1;
      }
      overflow = ring->overflow_count;
      spinlock_release(&ring->lock);

      if (!has_record) {
        break;   // this core's ring is empty — move on to the next core
      }

      uint8_t payload[5];
      payload[0] = record.core_id;
      payload[1] = (uint8_t)(record.task_id >> 8);
      payload[2] = (uint8_t)(record.task_id & 0xFFU);
      payload[3] = record.state;
      payload[4] = overflow;
      telemetry_send(PKT_TICK_STATE, payload, sizeof(payload));
    }
  }
}

void telemetry_publisher_task(void *params)
{
  (void)params;
  uint32_t heartbeat_counter = 0;
  uint32_t loop_counter = 0;

  while (1) {
    telemetry_drain_tick_rings();

    // Heartbeat every 100th iteration — preserves the original ~10 Hz rate
    // (Phase 1's validation cadence) while the loop itself now polls at
    // ~1 kHz to keep pace with tick-state production.
    if ((loop_counter % 100U) == 0U) {
      uint8_t payload[4] = {
        (uint8_t)(heartbeat_counter >> 24), (uint8_t)(heartbeat_counter >> 16), (uint8_t)(heartbeat_counter >> 8), (uint8_t)heartbeat_counter
      };
      telemetry_send(PKT_HEARTBEAT, payload, sizeof(payload));
      heartbeat_counter++;
    }
    loop_counter++;

    task_delay_ms(1U);
  }
}

#endif // RTOS_TELEMETRY
