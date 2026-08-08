#ifndef TELEMETRY_FRAME_H
#define TELEMETRY_FRAME_H

#include <stdint.h>

#include "telemetry.h"

#ifdef RTOS_TELEMETRY

/**
 * @brief Frame (magic/type/seq/len/payload/crc/trailer) and transmit one telemetry packet.
 * @note Not locked, not RTOS-dependent (only crc.h + uart.h) — every caller is responsible
 * for its own locking, or for being single-threaded (bootstrap/bootloader). Never call
 * directly from app code; go through telemetry_send() or a telemetry_report_*() wrapper
 * in telemetry.c, which hold telemetry_lock around this call.
 */
void telemetry_send_framed(TelemetryPacketType type, const uint8_t *payload, uint8_t len);

#endif // RTOS_TELEMETRY

#endif
