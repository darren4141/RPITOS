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

// Per-core tick-state ring depth — "a handful of ticks" of headroom against the
// producer (that core's ISR) briefly outrunning the consumer (core 3's poll loop).
// Not meant to absorb a sustained backlog — see overflow_count below for that signal.
#define TELEMETRY_TICK_RING_DEPTH 8U

typedef enum {
  PKT_HEARTBEAT      = 0,   // trivial packet type for Phase 1 protocol validation — see md/client/device/implementation_plan.md
  PKT_TASK_CREATED   = 1,   // task_id:2 + core_id:1 + priority:1 + name:up to TELEMETRY_TASK_NAME_MAX (not NUL-terminated — LEN implies length)
  PKT_TICK_STATE     = 2,   // core_id:1 + task_id:2 + state:1 + overflow_count:1 (5 bytes total)
  PKT_TASK_BLOCKED   = 3,   // task_id:2 + core_id:1 (3 bytes) — no reason field, see telemetry_report_task_unblocked()'s doc comment
  PKT_TASK_UNBLOCKED = 4,   // task_id:2 + core_id:1 (3 bytes)
  NUM_TELEMETRY_PACKET_TYPES
} TelemetryPacketType;

/**
 * @brief Frame and transmit one telemetry packet on the dedicated telemetry UART (uart_telemetry_tx_raw()).
 * @note uart_telemetry_init() must have run first. Not the same wire as UART0's
 * console — see transport_protocol.md. Maintains a per-type, wrapping sequence
 * counter so the host can detect drops. No-op if type is out of range.
 * @warning NOT safe to call concurrently from multiple cores — no locking. Only
 * call this from the single dedicated telemetry-publisher core (core 3 in
 * multicore_full_demo). Any packet type that could originate from more than
 * one core needs its own locked entry point instead — see
 * telemetry_report_task_created() for that shape.
 */
void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len);

/**
 * @brief Task entry point sending a periodic PKT_HEARTBEAT — the sole job of a dedicated telemetry core.
 */
void telemetry_publisher_task(void *params);

/**
 * @brief Format and send one PKT_TASK_CREATED report. Called once, at creation time, by task_create() and by the idle task's setup in scheduler_init() — the name is never stored anywhere on-device beyond this call's own stack.
 * @note name may be NULL (reported with a zero-length name field); truncated to TELEMETRY_TASK_NAME_MAX bytes if longer.
 * @note Safe to call from any core — internally locked, unlike telemetry_send(), because task_create() runs on whichever core is creating a task.
 */
void telemetry_report_task_created(uint16_t task_id, uint32_t core_id, uint8_t priority, const char *name);

/**
 * @brief Push one tick-state record into the calling core's ring. Call from scheduler_switch_context() every tick.
 * @note Safe to call from any core — one Spinlock per core, so contention is only ever between that core's own ISR and core 3's drain loop, never across app cores. Cheap by design: lock, copy ~5 bytes, unlock — never touches the UART itself. On a full ring, drops the new record and increments that core's overflow_count rather than blocking or overwriting.
 */
void telemetry_report_tick_state(uint32_t core_id, uint16_t task_id, uint8_t state);

/**
 * @brief Drain every core's tick-state ring and send one PKT_TICK_STATE per record found.
 * @note Call this from a polling loop on the dedicated telemetry-publisher core (see telemetry_publisher_task()) — not lock-free itself, but every telemetry_send() it triggers is, since only core 3 ever calls this.
 */
void telemetry_drain_tick_rings(void);

/**
 * @brief Report a task entering the blocked state. Call from scheduler_add_to_blocked_list().
 * @note Safe to call from any core — locked, same as telemetry_report_task_created(). Event-driven, not hot-path, so the lock's cost is irrelevant here (unlike telemetry_report_tick_state()).
 */
void telemetry_report_task_blocked(uint16_t task_id, uint32_t core_id);

/**
 * @brief Report a task leaving the blocked state, for any reason (timeout, mutex, or semaphore). Call from scheduler_remove_from_blocked_list().
 * @note Safe to call from any core. Deliberately carries no "why" field — TaskWakeupReason (task_types.h) is a semaphore.c-internal signal, not a scheduler-wide one: mutex_unlock() never sets it, and semaphore_give() sets it after the point this function is meant to be called from, so reading it here would misreport the one case it exists for. See instrumentation.md's note on this.
 */
void telemetry_report_task_unblocked(uint16_t task_id, uint32_t core_id);

#endif // RTOS_TELEMETRY

#endif
