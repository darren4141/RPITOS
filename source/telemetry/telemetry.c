#include "telemetry.h"

#ifdef RTOS_TELEMETRY

#include "crc.h"
#include "scheduler.h"
#include "spinlock.h"
#include "uart.h"

static uint8_t seq_counters[NUM_TELEMETRY_PACKET_TYPES] = { 0 };

// Guards telemetry_report_task_created() only — see its doc comment in
// telemetry.h. Every other packet type (PKT_HEARTBEAT now; PKT_TICK_STATE/
// PKT_TASK_BLOCKED/PKT_TASK_UNBLOCKED later, per the implementation plan) is
// sent exclusively by the dedicated core-3 publisher — single-writer by
// design, so telemetry_send() itself stays lock-free and those calls pay
// zero locking overhead, including once PKT_TICK_STATE pushes this to ~1 kHz.
// Zero-initialized by BSS, which is already this lock's unlocked state (see
// spinlock_init()) — no explicit init call needed.
static Spinlock telemetry_lock;

// Actual framing + CRC + UART write. No locking — callers decide whether
// they need it. telemetry_send() (below) calls this directly, trusting the
// caller to be the single dedicated telemetry core; telemetry_report_task_created()
// wraps this in telemetry_lock instead, since task_create()/scheduler_init()
// call it from whichever core creates a task.
static void telemetry_send_framed(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
{
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

void telemetry_send(TelemetryPacketType type, const uint8_t *payload, uint8_t len)
{
  if (type >= NUM_TELEMETRY_PACKET_TYPES) {
    return;
  }

  telemetry_send_framed(type, payload, len);
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

  // Locked: this is the one call path with genuinely concurrent multi-core
  // callers (any core, whenever it creates a task) — see telemetry_lock's
  // doc comment above.
  spinlock_acquire(&telemetry_lock);
  telemetry_send_framed(PKT_TASK_CREATED, payload, (uint8_t)(4 + name_len));
  spinlock_release(&telemetry_lock);
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
