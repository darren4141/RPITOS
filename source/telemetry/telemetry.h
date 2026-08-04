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
  PKT_HEARTBEAT      = 0,
  PKT_TASK_CREATED   = 1,   // task_id:2 + core_id:1 + priority:1 + name:up to TELEMETRY_TASK_NAME_MAX (not NUL-terminated)
  PKT_TICK_STATE     = 2,   // core_id:1 + task_id:2 + state:1 + overflow_count:1 (5 bytes total)
  PKT_TASK_BLOCKED   = 3,   // task_id:2 + core_id:1 (3 bytes)
  PKT_TASK_UNBLOCKED = 4,   // task_id:2 + core_id:1 (3 bytes)
  NUM_TELEMETRY_PACKET_TYPES
} TelemetryPacketType;

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
 * @brief Report a task entering the blocked state. Call from block_until(), semaphore_take(), and mutex_lock().
 * @note Safe to call from any core, including IRQ context.
 */
void telemetry_report_task_blocked(uint16_t task_id, uint32_t core_id);

/**
 * @brief Report a task leaving the blocked state, for any reason (timeout, mutex, or semaphore). Call from timer_tick_handler(), semaphore_give(), and mutex_unlock().
 * @note Safe to call from any core, including IRQ context. Carries no "why" field.
 */
void telemetry_report_task_unblocked(uint16_t task_id, uint32_t core_id);

#endif // RTOS_TELEMETRY

#endif
