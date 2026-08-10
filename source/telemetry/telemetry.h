#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

#ifdef RTOS_TELEMETRY

// Wire framing — see md/client/transport_protocol.md:
// [0xA5][TYPE:1][SEQ:1][LEN_MSB:1][LEN_LSB:1][PAYLOAD:LEN][CRC32:4 LE][0x5A]
// CRC32 covers everything before the CRC field (magic through payload).
#define TELEMETRY_MAGIC         0xA5U
#define TELEMETRY_TRAILER       0x5AU

#define TELEMETRY_TASK_NAME_MAX 16U

#define TELEMETRY_TICK_RING_DEPTH 8U   // per-core tick-state ring depth

typedef enum {
  PKT_HEARTBEAT        = 0,
  PKT_TASK_CREATED     = 1,   // task_id:2 + core_id:1 + priority:1 + name:up to TELEMETRY_TASK_NAME_MAX (not NUL-terminated)
  PKT_TICK_STATE       = 2,   // core_id:1 + task_id:2 + state:1 + overflow_count:1 (5 bytes total)
  PKT_TASK_BLOCKED     = 3,   // task_id:2 + core_id:1 + sync_kind:1 + sync_id:2 (6 bytes)
  PKT_TASK_UNBLOCKED   = 4,   // task_id:2 + core_id:1 (3 bytes)
  PKT_BOOT_STAGE_ENTER = 5,   // stage:1 + reserved:4 (currently always 0) — 5 bytes. See md/client/device/boot_init_tracking.md.
  PKT_BOOT_MILESTONE   = 6,   // milestone_id:1 + stage:1 + counter:4 (0, except core_id for the 3 per-core milestones) — 6 bytes
  PKT_BOOT_INFO         = 7,  // sub_type:1 + fields... (variable, see TelemetryBootInfoSubtype)
  PKT_DFU_EVENT          = 8, // phase:1 + target:1 + extra:4 — 6 bytes
  PKT_SYNC_CREATED        = 9,  // kind:1 + sync_id:2 + parent_sync_id:2 (0xFFFF=none) + name:variable (not NUL-terminated)
  PKT_MUTEX_OWNER_CHANGED = 10, // sync_id:2 + has_owner:1 + owner_task_id:2 + owner_core_id:1 — 6 bytes
  NUM_TELEMETRY_PACKET_TYPES
} TelemetryPacketType;

// ── Sync primitive tracking (mutex/semaphore/queue) ─────────────────────────
// See md/client/host/sync_view.md.

#define TELEMETRY_SYNC_NAME_MAX 16U
#define TELEMETRY_SYNC_ID_NONE  0xFFFFU   // parent_sync_id "no parent" / owner_task_id unused when has_owner=0

// Bound on how many sync objects telemetry_register_sync() remembers for
// re-announcement. multicore_full_demo registers 12 today; see docs.md.
#define TELEMETRY_MAX_SYNC_OBJECTS 16U

typedef enum {
  SYNC_KIND_MUTEX     = 0,
  SYNC_KIND_SEMAPHORE = 1,
  SYNC_KIND_QUEUE     = 2,
  SYNC_KIND_NONE      = 0xFF,   // block_until()/task_delay — not blocked on a sync object at all
} TelemetrySyncKind;

// ── Boot / DFU tracking — see md/client/device/boot_init_tracking.md ───────
// Boot-phase timing is deliberately not device-timestamped: the host times
// phases itself from packet arrival (already-resolved project stance — see
// that doc). These packet types carry identity/ordering information only.

typedef enum {
  BOOT_STAGE_BOOTSTRAP  = 0,
  BOOT_STAGE_BOOTLOADER = 1,
  BOOT_STAGE_APP        = 2,
} TelemetryBootStage;

// One flat enum across all three stages — `stage` (carried alongside every
// milestone) disambiguates which image sent it; a milestone id is never
// ambiguous once paired with its stage.
typedef enum {
  BOOT_MS_EMMC_INIT_DONE               = 0,   // bootstrap + bootloader
  BOOT_MS_EMMC_INIT_FAILED             = 1,   // bootstrap + bootloader
  BOOT_MS_HEADER_READ_FAILED           = 2,   // bootstrap only — hang path
  BOOT_MS_BAD_HEADER                   = 3,   // bootstrap only — hang path (fw_length == 0)
  BOOT_MS_BINARY_READ_FAILED           = 4,   // bootstrap only — hang path
  BOOT_MS_JUMPING_TO_BOOTLOADER        = 5,   // bootstrap only
  BOOT_MS_JTAG_INIT_DONE               = 6,   // bootloader only
  BOOT_MS_BOOT_MODULE_INIT_DONE        = 7,   // bootloader only
  BOOT_MS_DFU_MODULE_INIT_DONE         = 8,   // bootloader only
  BOOT_MS_TRIAL_ROLLBACK               = 9,   // bootloader only — unconfirmed trial slot rolled back
  BOOT_MS_TOLERANCE_EXCEEDED_FORCE_DFU = 10,  // bootloader only
  BOOT_MS_TOLERANCE_EXCEEDED_ROLLBACK  = 11,  // bootloader only
  BOOT_MS_ALL_RETRIES_EXHAUSTED        = 12,  // bootloader only — entering the final DFU recovery loop
  BOOT_MS_APP_LOAD_DONE                = 13,  // bootloader only
  BOOT_MS_JUMPING_TO_APP               = 14,  // bootloader only
  BOOT_MS_GIC_INIT_DONE                = 15,  // app only
  BOOT_MS_GENTIMER_INIT_DONE           = 16,  // app only
  BOOT_MS_CORE_RELEASED                = 17,  // app only; counter = core_id
  BOOT_MS_SCHEDULER_INIT_DONE          = 18,  // app only; counter = core_id
  BOOT_MS_SCHEDULER_START              = 19,  // app only; counter = core_id
  BOOT_MS_SLOT_CONFIRMED               = 20,  // app only — wdt_meta_confirm_slot() called
} TelemetryBootMilestoneId;

// PKT_BOOT_INFO sub-types — one packet type, discriminated by sub_type, since
// each is "dump a known struct verbatim" rather than a new shape.
typedef enum {
  // sub_type:1(=0) + stage:1 + version_num:4 + fw_length:4 + expected_crc:4 + actual_crc:4 + crc_ok:1 — 19 bytes.
  // Sent by bootstrap (validating the bootloader image) and by bootloader (validating the active app slot).
  BOOT_INFO_IMAGE_HEADER = 0,
  // sub_type:1(=1) + active_app_slot:1(0=A/1=B) + app_slot_trial:1 + trial_boot_count:4 +
  // wdt_reset_count:4 + wdt_reset_tolerance:4 + wdt_reset_policy:1 + wdt_reset_reason:1 — 17 bytes.
  // Sent by bootloader right after wdt_meta_read().
  BOOT_INFO_SLOT_STATE = 1,
  // sub_type:1(=2) + reset_reason:1 + dfu_requested:1 + fw_crc_ok:1 — 4 bytes.
  // Sent by bootloader after boot_flags_init()/revalidation settles.
  BOOT_INFO_BOOT_FLAGS = 2,
} TelemetryBootInfoSubtype;

// Which image a PKT_DFU_EVENT concerns — a self-update (CMD_START_SELF_UPDATE)
// writes the bootloader's own eMMC slot, a materially different flow from a
// normal app flash.
typedef enum {
  DFU_TARGET_APP        = 0,
  DFU_TARGET_BOOTLOADER = 1,      // CMD_START_SELF_UPDATE
  DFU_TARGET_UNKNOWN    = 0xFF,   // not yet known — before CMD_START/_SELF_UPDATE arrives
} TelemetryDfuTarget;

typedef enum {
  DFU_EVT_WAITING_TRIGGER   = 0,   // entered dfu_receive(), blocked on the 4-byte trigger key
  DFU_EVT_TRIGGER_MATCHED   = 1,
  DFU_EVT_START_RECEIVED    = 2,   // extra = img_length (from StartPacket.fw_length)
  DFU_EVT_DATA_PROGRESS     = 3,   // extra = bytes_hashed so far — throttled to ~1%-of-img_length steps
  DFU_EVT_CRC_CHECK_RESULT  = 4,   // extra = 1 (pass) / 0 (fail)
  DFU_EVT_READBACK_VALIDATE = 5,   // extra = 1 / 0 — app target only, no-op for self-update
  DFU_EVT_MARKER_CHECK      = 6,   // extra = 1 / 0 — app target only, no-op for self-update
  DFU_EVT_COMMITTED         = 7,   // extra = new active_app_slot (0=A/1=B); unused (0) for self-update
  DFU_EVT_ABORTED           = 8,   // extra = TelemetryDfuAbortReason
} TelemetryDfuEventPhase;

typedef enum {
  DFU_ABORT_BAD_START_LEN   = 0,   // CMD_START/_SELF_UPDATE payload wasn't sizeof(StartPacket)
  DFU_ABORT_ALREADY_STARTED = 1,   // a second CMD_START/_SELF_UPDATE within one session
  DFU_ABORT_BAD_DATA_LEN    = 2,   // CMD_DATA payload not a multiple of 4
  DFU_ABORT_HOST_ABORT      = 3,   // CMD_ABORT received
  DFU_ABORT_CRC_MISMATCH    = 4,   // CMD_FINISH: streamed CRC != StartPacket.crc
  DFU_ABORT_READBACK_FAILED = 5,   // written slot failed read-back validation (app target only)
  DFU_ABORT_MARKER_MISSING  = 6,   // DFU-support marker missing/wrong (app target only)
  DFU_ABORT_TIMEOUT         = 7,   // dfu_packet_receive() timed out mid-session
} TelemetryDfuAbortReason;

/**
 * @brief Frame and transmit one telemetry packet on the dedicated telemetry UART.
 * @note uart_telemetry_init() must have run first.
 * @note Safe to call from any core — internally locked (telemetry_lock, telemetry.c).
 */
void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len);

/**
 * @brief Task entry point sending a periodic PKT_HEARTBEAT and draining the tick-state rings.
 */
void telemetry_publisher_task(void *params);

/**
 * @brief Format and send one PKT_TASK_CREATED report at task-creation time.
 * @note name may be NULL (reported with a zero-length name field); truncated to TELEMETRY_TASK_NAME_MAX bytes if longer.
 * @note Safe to call from any core. Caller must mask IRQs (enter_critical()) unless already running with IRQs masked.
 */
void telemetry_report_task_created(uint16_t task_id, uint32_t core_id, uint8_t priority, const char *name);

/**
 * @brief Push one tick-state record into the calling core's ring. Call from scheduler_switch_context() every tick.
 * @note Safe to call from any core. On a full ring, drops the new record and increments that core's overflow_count.
 */
void telemetry_report_tick_state(uint32_t core_id, uint16_t task_id, uint8_t state);

/**
 * @brief Drain every core's tick-state ring and send one PKT_TICK_STATE per record found.
 * @note Call from a polling loop on the dedicated telemetry-publisher core.
 */
void telemetry_drain_tick_rings(void);

/**
 * @brief Report a task entering the blocked state, and what (if anything) it's blocked on. Call from block_until() (sync_kind = SYNC_KIND_NONE, sync_id ignored), semaphore_take() (SYNC_KIND_SEMAPHORE, smph->sync_id), and mutex_lock() (SYNC_KIND_MUTEX, mtx->sync_id).
 * @note Safe to call from any core, including IRQ context.
 */
void telemetry_report_task_blocked(uint16_t task_id, uint32_t core_id, TelemetrySyncKind sync_kind, uint16_t sync_id);

/**
 * @brief Report a task leaving the blocked state, for any reason (timeout, mutex, or semaphore). Call from timer_tick_handler(), semaphore_give(), and mutex_unlock().
 * @note Safe to call from any core, including IRQ context. Carries no "why"/which-object field —
 * the host already recorded what this task was blocked on from the matching telemetry_report_task_blocked() call, and clears it on any unblock regardless of cause.
 */
void telemetry_report_task_unblocked(uint16_t task_id, uint32_t core_id);

/**
 * @brief Register a sync object (mutex/semaphore/queue), broadcast its identity immediately, and remember it for later re-announcement (see telemetry_rebroadcast_sync_roster()).
 * @param parent_sync_id TELEMETRY_SYNC_ID_NONE, or another object's sync_id — used by queue_init() to link its two internal semaphores back to the queue that owns them.
 * @param name May be NULL (reported with a zero-length name field); truncated to TELEMETRY_SYNC_NAME_MAX bytes if longer. Copied into the registry (unlike telemetry_report_task_created()'s name, which is never stored) so it can be re-sent later — safe here since the registry is small and bounded, not per-task-control-block state.
 * @return The assigned sync_id — store it on the object (e.g. mtx->sync_id) for later telemetry_report_task_blocked()/telemetry_report_mutex_owner_changed() calls.
 * @note Safe to call from any core. Caller must mask IRQs (enter_critical()) unless already running with IRQs masked — same requirement as telemetry_report_task_created().
 */
uint16_t telemetry_register_sync(TelemetrySyncKind kind, uint16_t parent_sync_id, const char *name);

/**
 * @brief Re-send PKT_SYNC_CREATED for every sync object registered so far this boot.
 * @note PKT_SYNC_CREATED is otherwise a one-shot broadcast with no replay — real hardware testing found the initial send can be lost either to a host that wasn't listening yet or to framing corruption during the power-up/reset transient (the CRC check correctly rejects the corrupted frame; the data just never arrives). Call periodically (see telemetry_publisher_task()) so the roster becomes eventually consistent regardless of why the first attempt was lost. Idempotent on the host side — re-inserting the same sync_id with the same fields is a no-op there.
 * @note Call from the telemetry-publisher core only (same as telemetry_publisher_task() itself) — not IRQ-safe, does real work (a loop + UART writes) per call.
 */
void telemetry_rebroadcast_sync_roster(void);

/**
 * @brief Report a mutex's ownership changing. Call from mutex_lock() (immediate acquire) and mutex_unlock() (release to none, or handoff to the next owner).
 * @note has_owner = 0 means unowned — owner_task_id/owner_core_id are unused (send 0) in that case.
 */
void telemetry_report_mutex_owner_changed(uint16_t sync_id, uint8_t has_owner, uint16_t owner_task_id, uint32_t owner_core_id);

// ── Boot / DFU tracking ─────────────────────────────────────────────────────
// Defined by telemetry_boot.c (bootstrap/bootloader, unlocked) or telemetry.c
// (app, locked) — exactly one links per sample. See docs.md.

/**
 * @brief Report entry into a boot stage (bootstrap/bootloader/app). Call once, right after uart_telemetry_init().
 */
void telemetry_report_boot_stage_enter(TelemetryBootStage stage);

/**
 * @brief Report a boot-sequence milestone. `counter` is 0 except for the three per-core app milestones (BOOT_MS_CORE_RELEASED/SCHEDULER_INIT_DONE/SCHEDULER_START), where it carries core_id.
 */
void telemetry_report_boot_milestone(TelemetryBootMilestoneId id, TelemetryBootStage stage, uint32_t counter);

/**
 * @brief Report a validated image's header + CRC result (StartPacket fields). Call from bootstrap (validating the bootloader) and bootloader (validating the active app slot via boot_validate_app()).
 */
void telemetry_report_boot_info_image_header(TelemetryBootStage stage, uint32_t version_num, uint32_t fw_length,
                                              uint32_t expected_crc, uint32_t actual_crc, uint8_t crc_ok);

/**
 * @brief Report the A/B slot + watchdog-metadata snapshot. Call from bootloader right after wdt_meta_read().
 * @note active_app_slot: 0 = A, 1 = B (already mapped from APP_SLOT_A/APP_SLOT_B by the caller).
 */
void telemetry_report_boot_info_slot_state(uint8_t active_app_slot, uint8_t app_slot_trial, uint32_t trial_boot_count,
                                            uint32_t wdt_reset_count, int32_t wdt_reset_tolerance,
                                            uint8_t wdt_reset_policy, uint8_t wdt_reset_reason);

/**
 * @brief Report the settled BootFlags snapshot. Call from bootloader after boot_flags_init()/revalidation.
 */
void telemetry_report_boot_info_boot_flags(uint8_t reset_reason, uint8_t dfu_requested, uint8_t fw_crc_ok);

/**
 * @brief Report a DFU protocol event/phase transition. Call from dfu_receive.c at each state transition.
 * @note target is DFU_TARGET_UNKNOWN until CMD_START/_SELF_UPDATE arrives and pins it for the rest of the session.
 */
void telemetry_report_dfu_event(TelemetryDfuEventPhase phase, uint8_t target, uint32_t extra);

#endif // RTOS_TELEMETRY

#endif
