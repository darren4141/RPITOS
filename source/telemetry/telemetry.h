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

// Per-core tick-state ring depth — headroom against the producer (that
// core's ISR) briefly outrunning the consumer (core 3's poll loop). Not
// meant to absorb a sustained backlog — see overflow_count below.
#define TELEMETRY_TICK_RING_DEPTH 8U

typedef enum {
  PKT_HEARTBEAT      = 0,   // see md/client/device/implementation_plan.md Phase 1
  PKT_TASK_CREATED   = 1,   // task_id:2 + core_id:1 + priority:1 + name:up to TELEMETRY_TASK_NAME_MAX (not NUL-terminated — LEN implies length)
  PKT_TICK_STATE     = 2,   // core_id:1 + task_id:2 + state:1 + overflow_count:1 (5 bytes total)
  PKT_TASK_BLOCKED   = 3,   // task_id:2 + core_id:1 (3 bytes) — no reason field, see telemetry_report_task_unblocked()'s doc comment
  PKT_TASK_UNBLOCKED = 4,   // task_id:2 + core_id:1 (3 bytes)
  NUM_TELEMETRY_PACKET_TYPES
} TelemetryPacketType;

/**
 * @brief Frame and transmit one telemetry packet on the dedicated telemetry UART.
 * @note uart_telemetry_init() must have run first. Not the same wire as UART0's console.
 * @note Safe to call from any core — internally locked (telemetry_lock, telemetry.c).
 * Every telemetry_report_*() wrapper shares the same lock, so no caller can ever
 * interleave on the wire with another. See telemetry.c's comment on telemetry_lock
 * for why every caller needs it, including this one (md/client/device/instrumentation.md's
 * Third postmortem has the hardware bug this fixed).
 */
void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len);

/**
 * @brief Task entry point sending a periodic PKT_HEARTBEAT and draining the tick-state rings — the sole job of a dedicated telemetry core.
 */
void telemetry_publisher_task(void *params);

/**
 * @brief Format and send one PKT_TASK_CREATED report. Called once, at creation time, by task_create() and by the idle task's setup in scheduler_init() — the name is never stored anywhere on-device beyond this call's own stack.
 * @note name may be NULL (reported with a zero-length name field); truncated to TELEMETRY_TASK_NAME_MAX bytes if longer.
 * @note Safe to call from any core. Callers must wrap this in enter_critical()/exit_critical()
 * unless already running with IRQs masked — see task.c's task_create() call site for why.
 */
void telemetry_report_task_created(uint16_t task_id, uint32_t core_id, uint8_t priority, const char *name);

/**
 * @brief Push one tick-state record into the calling core's ring. Call from scheduler_switch_context() every tick.
 * @note Safe to call from any core — one Spinlock per core, contention only between that core's own ISR and core 3's drain loop. Cheap by design: lock, copy ~5 bytes, unlock — never touches the UART. On a full ring, drops the new record and increments that core's overflow_count.
 */
void telemetry_report_tick_state(uint32_t core_id, uint16_t task_id, uint8_t state);

/**
 * @brief Drain every core's tick-state ring and send one PKT_TICK_STATE per record found.
 * @note Call from a polling loop on the dedicated telemetry-publisher core (see telemetry_publisher_task()).
 */
void telemetry_drain_tick_rings(void);

/**
 * @brief Report a task entering the blocked state. Call from block_until(), semaphore_take(), and mutex_lock() — see md/client/device/instrumentation.md's "Where to hook" for why those three, not scheduler_add_to_blocked_list().
 * @note Safe to call from any core, including IRQ context — see telemetry_report_task_unblocked()'s doc comment.
 */
void telemetry_report_task_blocked(uint16_t task_id, uint32_t core_id);

/**
 * @brief Report a task leaving the blocked state, for any reason (timeout, mutex, or semaphore). Call from timer_tick_handler(), semaphore_give(), and mutex_unlock() — see md/client/device/instrumentation.md's "Where to hook" for why those three, not scheduler_remove_from_blocked_list().
 * @note Safe to call from any core, including IRQ context (timer_tick_handler's timeout path) — every telemetry_lock-touching call site runs with IRQs already masked (own enter_critical(), or is IRQ context outright), so this core's own timer IRQ can never preempt another call mid-lock. See instrumentation.md's Third postmortem follow-up for the full check. Deliberately carries no "why" field — TaskWakeupReason (task_types.h) is a semaphore.c-internal signal, not a scheduler-wide one; see instrumentation.md's correction on this.
 */
void telemetry_report_task_unblocked(uint16_t task_id, uint32_t core_id);

#endif // RTOS_TELEMETRY

#endif
