#include "telemetry.h"

#ifdef RTOS_TELEMETRY

#include "crc.h"
#include "scheduler.h"
#include "uart.h"

static uint8_t seq_counters[NUM_TELEMETRY_PACKET_TYPES] = { 0 };

void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
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

void telemetry_publisher_task(void *params)
{
  (void)params;
  uint32_t counter = 0;

  while (1) {
    uint8_t payload[4] = {
      (uint8_t)(counter >> 24), (uint8_t)(counter >> 16), (uint8_t)(counter >> 8), (uint8_t)counter
    };
    telemetry_send(PKT_HEARTBEAT, payload, sizeof(payload));
    counter++;
    task_delay_ms(100U);   // 10 Hz — plenty to validate CRC/resync/seq-gaps, see implementation_plan.md Phase 1
  }
}

#endif // RTOS_TELEMETRY
