#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

#ifdef RTOS_TELEMETRY

// Wire framing — see md/client/transport_protocol.md:
//   [0xA5][TYPE:1][SEQ:1][LEN_MSB:1][LEN_LSB:1][PAYLOAD:LEN][CRC32:4 LE][0x5A]
// CRC32 covers everything before the CRC field (magic through payload).
#define TELEMETRY_MAGIC   0xA5U
#define TELEMETRY_TRAILER 0x5AU

typedef enum {
  PKT_HEARTBEAT = 0,   // trivial packet type for Phase 1 protocol validation — see md/client/implementation_plan.md
  NUM_TELEMETRY_PACKET_TYPES
} TelemetryPacketType;

/**
 * @brief Frame and transmit one telemetry packet on the dedicated telemetry UART (uart_telemetry_tx_raw()).
 * @note uart_telemetry_init() must have run first. Not the same wire as UART0's
 * console — see transport_protocol.md. Maintains a per-type, wrapping sequence
 * counter so the host can detect drops. No-op if type is out of range.
 */
void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len);

/**
 * @brief Task entry point sending a periodic PKT_HEARTBEAT — the sole job of a dedicated telemetry core.
 */
void telemetry_publisher_task(void *params);

#endif // RTOS_TELEMETRY

#endif
