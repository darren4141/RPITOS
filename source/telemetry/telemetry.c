#include "telemetry.h"

#ifdef RTOS_TELEMETRY
#include <stddef.h>

#include "companion_core.h"
#include "scheduler.h"
#include "spinlock.h"
#include "telemetry_frame.h"
#include "uart.h"

// Guards every call into telemetry_send_framed() (telemetry_frame.c), from
// every entry point in this file. Zero-initialized by BSS — already
// unlocked, no explicit init needed.
static Spinlock telemetry_lock;

void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
{
  // telemetry_send_framed() (telemetry_frame.c) itself validates type.
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(type, payload, len);
  spinlock_release(&telemetry_lock);
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

  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_TASK_CREATED, payload, (uint8_t)(4 + name_len));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_task_blocked(uint16_t task_id, uint32_t core_id, TelemetrySyncKind sync_kind, uint16_t sync_id)
{
  uint8_t payload[6];
  payload[0] = (uint8_t)(task_id >> 8);
  payload[1] = (uint8_t)(task_id & 0xFFU);
  payload[2] = (uint8_t)core_id;
  payload[3] = (uint8_t)sync_kind;
  payload[4] = (uint8_t)(sync_id >> 8);
  payload[5] = (uint8_t)(sync_id & 0xFFU);

  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_TASK_BLOCKED, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_task_unblocked(uint16_t task_id, uint32_t core_id)
{
  uint8_t payload[3];
  payload[0] = (uint8_t)(task_id >> 8);
  payload[1] = (uint8_t)(task_id & 0xFFU);
  payload[2] = (uint8_t)core_id;

  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_TASK_UNBLOCKED, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

// ── Sync primitive tracking (mutex/semaphore/queue) ─────────────────────────

// Assigns sync_ids in creation order, across every kind (a mutex, a semaphore, and
// a queue's two internal semaphores all draw from the same counter) — mirrors
// task_id assignment's spirit, just simpler since there's no per-core scoping
// question here (a sync object isn't owned by any one core). Guarded by
// telemetry_lock, same as the send itself, so allocate+broadcast is atomic —
// two racing registrations can never observe/send the same id.
static uint16_t next_sync_id = 0;

uint16_t telemetry_register_sync(TelemetrySyncKind kind, uint16_t parent_sync_id, const char *name)
{
  uint8_t payload[5 + TELEMETRY_SYNC_NAME_MAX];

  spinlock_acquire(&telemetry_lock);

  uint16_t sync_id = next_sync_id++;

  payload[0] = (uint8_t)kind;
  payload[1] = (uint8_t)(sync_id >> 8);
  payload[2] = (uint8_t)(sync_id & 0xFFU);
  payload[3] = (uint8_t)(parent_sync_id >> 8);
  payload[4] = (uint8_t)(parent_sync_id & 0xFFU);

  uint8_t name_len = 0;
  while ((name != NULL) && (name[name_len] != '\0') && (name_len < TELEMETRY_SYNC_NAME_MAX)) {
    payload[5 + name_len] = (uint8_t)name[name_len];
    name_len++;
  }

  telemetry_send_framed(PKT_SYNC_CREATED, payload, (uint8_t)(5 + name_len));
  spinlock_release(&telemetry_lock);

  return sync_id;
}

void telemetry_report_mutex_owner_changed(uint16_t sync_id, uint8_t has_owner, uint16_t owner_task_id, uint32_t owner_core_id)
{
  uint8_t payload[6];
  payload[0] = (uint8_t)(sync_id >> 8);
  payload[1] = (uint8_t)(sync_id & 0xFFU);
  payload[2] = has_owner;
  payload[3] = (uint8_t)(owner_task_id >> 8);
  payload[4] = (uint8_t)(owner_task_id & 0xFFU);
  payload[5] = (uint8_t)owner_core_id;

  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_MUTEX_OWNER_CHANGED, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

// ── Boot / DFU tracking (locked wrappers — app side) ────────────────────────
// The app only ever calls the stage/milestone/slot-confirmed subset of these
// (from main.c, scheduler.c, companion_core.c — none of them dfu_receive.c,
// which never links into an app build). The BOOT_INFO/DFU_EVENT wrappers are
// still defined here for API symmetry with telemetry_boot.c, even though
// nothing in a normal app build calls them today.

void telemetry_report_boot_stage_enter(TelemetryBootStage stage)
{
  uint8_t payload[5] = { (uint8_t)stage, 0, 0, 0, 0 };   // reserved bytes always 0 — see telemetry.h
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_BOOT_STAGE_ENTER, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_boot_milestone(TelemetryBootMilestoneId id, TelemetryBootStage stage, uint32_t counter)
{
  uint8_t payload[6];
  payload[0] = (uint8_t)id;
  payload[1] = (uint8_t)stage;
  payload[2] = (uint8_t)(counter >> 24);
  payload[3] = (uint8_t)(counter >> 16);
  payload[4] = (uint8_t)(counter >> 8);
  payload[5] = (uint8_t)counter;
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_BOOT_MILESTONE, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_boot_info_image_header(TelemetryBootStage stage, uint32_t version_num, uint32_t fw_length,
                                             uint32_t expected_crc, uint32_t actual_crc, uint8_t crc_ok)
{
  uint8_t payload[19];
  payload[0] = (uint8_t)BOOT_INFO_IMAGE_HEADER;
  payload[1] = (uint8_t)stage;
  payload[2] = (uint8_t)(version_num >> 24);
  payload[3] = (uint8_t)(version_num >> 16);
  payload[4] = (uint8_t)(version_num >> 8);
  payload[5] = (uint8_t)version_num;
  payload[6] = (uint8_t)(fw_length >> 24);
  payload[7] = (uint8_t)(fw_length >> 16);
  payload[8] = (uint8_t)(fw_length >> 8);
  payload[9] = (uint8_t)fw_length;
  payload[10] = (uint8_t)(expected_crc >> 24);
  payload[11] = (uint8_t)(expected_crc >> 16);
  payload[12] = (uint8_t)(expected_crc >> 8);
  payload[13] = (uint8_t)expected_crc;
  payload[14] = (uint8_t)(actual_crc >> 24);
  payload[15] = (uint8_t)(actual_crc >> 16);
  payload[16] = (uint8_t)(actual_crc >> 8);
  payload[17] = (uint8_t)actual_crc;
  payload[18] = crc_ok;
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_BOOT_INFO, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_boot_info_slot_state(uint8_t active_app_slot, uint8_t app_slot_trial, uint32_t trial_boot_count,
                                           uint32_t wdt_reset_count, int32_t wdt_reset_tolerance,
                                           uint8_t wdt_reset_policy, uint8_t wdt_reset_reason)
{
  uint8_t payload[17];
  payload[0] = (uint8_t)BOOT_INFO_SLOT_STATE;
  payload[1] = active_app_slot;
  payload[2] = app_slot_trial;
  payload[3] = (uint8_t)(trial_boot_count >> 24);
  payload[4] = (uint8_t)(trial_boot_count >> 16);
  payload[5] = (uint8_t)(trial_boot_count >> 8);
  payload[6] = (uint8_t)trial_boot_count;
  payload[7] = (uint8_t)(wdt_reset_count >> 24);
  payload[8] = (uint8_t)(wdt_reset_count >> 16);
  payload[9] = (uint8_t)(wdt_reset_count >> 8);
  payload[10] = (uint8_t)wdt_reset_count;
  payload[11] = (uint8_t)((uint32_t)wdt_reset_tolerance >> 24);
  payload[12] = (uint8_t)((uint32_t)wdt_reset_tolerance >> 16);
  payload[13] = (uint8_t)((uint32_t)wdt_reset_tolerance >> 8);
  payload[14] = (uint8_t)(uint32_t)wdt_reset_tolerance;
  payload[15] = wdt_reset_policy;
  payload[16] = wdt_reset_reason;
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_BOOT_INFO, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_boot_info_boot_flags(uint8_t reset_reason, uint8_t dfu_requested, uint8_t fw_crc_ok)
{
  uint8_t payload[4] = { (uint8_t)BOOT_INFO_BOOT_FLAGS, reset_reason, dfu_requested, fw_crc_ok };
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_BOOT_INFO, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

void telemetry_report_dfu_event(TelemetryDfuEventPhase phase, uint8_t target, uint32_t extra)
{
  uint8_t payload[6];
  payload[0] = (uint8_t)phase;
  payload[1] = target;
  payload[2] = (uint8_t)(extra >> 24);
  payload[3] = (uint8_t)(extra >> 16);
  payload[4] = (uint8_t)(extra >> 8);
  payload[5] = (uint8_t)extra;
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_DFU_EVENT, payload, sizeof(payload));
  spinlock_release(&telemetry_lock);
}

// ── Per-core tick-state rings ──────────────────────────────────────────────
// One ring + one Spinlock per core. Producer: that core's own
// scheduler_switch_context(). Consumer: the publisher's drain loop.

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
  uint8_t overflow_count;   // records dropped while the ring was full; wraps
  Spinlock lock;
} TelemetryTickRing;

// Zero-initialized by BSS — head=tail=count=0 and an unlocked Spinlock are
// already the correct starting state.
static TelemetryTickRing g_tick_rings[COMPANION_CORE_MAX_CORES];

void telemetry_report_tick_state(uint32_t core_id, uint16_t task_id, uint8_t state)
{
  if (core_id >= COMPANION_CORE_MAX_CORES) {
    return;
  }

  TelemetryTickRing *ring = &g_tick_rings[core_id];

  spinlock_acquire(&ring->lock);

  if (ring->count == TELEMETRY_TICK_RING_DEPTH) {
    ring->overflow_count++;   // drop the new record, not the oldest
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

    // Heartbeat every 100th iteration (~10 Hz at a ~1 kHz loop rate).
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
