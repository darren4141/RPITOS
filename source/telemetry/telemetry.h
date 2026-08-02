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

typedef enum {
  PKT_HEARTBEAT    = 0,   // trivial packet type for Phase 1 protocol validation — see md/client/device/implementation_plan.md
  PKT_TASK_CREATED = 1,   // task_id:2 + core_id:1 + priority:1 + name:up to TELEMETRY_TASK_NAME_MAX (not NUL-terminated — LEN implies length)
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

#endif // RTOS_TELEMETRY

#endif
