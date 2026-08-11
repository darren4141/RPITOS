// Wire framing shared by every telemetry sender in the tree — the locked,
// RTOS-aware wrappers in telemetry.c (app) and the unlocked, single-threaded
// senders in telemetry_boot.c (bootstrap/bootloader). Kept dependency-free
// (only crc.h + uart.h) specifically so it links into UART_MINIMAL builds
// with no RTOS present. See md/client/device/boot_init_tracking.md's
// "Proposed transport" section for why this was split out of telemetry.c.

#include "telemetry_frame.h"

#ifdef RTOS_TELEMETRY

#include "crc.h"
#include "uart.h"

static uint8_t seq_counters[NUM_TELEMETRY_PACKET_TYPES] = { 0 };

void telemetry_send_framed(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
{
  if (type >= NUM_TELEMETRY_PACKET_TYPES) {
    return;
  }

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
    uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, header[i]);
  }
  for (uint8_t i = 0; i < len; i++) {
    uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, payload[i]);
  }

  uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, (uint8_t)(crc & 0xFFU));           // CRC32 LE
  uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, (uint8_t)((crc >> 8) & 0xFFU));
  uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, (uint8_t)((crc >> 16) & 0xFFU));
  uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, (uint8_t)((crc >> 24) & 0xFFU));

  uart_channel_tx_raw(UART_CHANNEL_TELEMETRY, TELEMETRY_TRAILER);
}

#endif // RTOS_TELEMETRY
