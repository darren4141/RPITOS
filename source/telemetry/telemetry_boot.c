// Boot/DFU telemetry senders for the bootstrap and bootloader images —
// UART_MINIMAL builds with no RTOS (no companion_core.h/scheduler.h/
// spinlock.h available). Unlocked: both images are single-threaded and
// blocking end to end, so there is never a second caller to race. Only
// telemetry_frame.c's telemetry_send_framed() is shared with the app's
// locked wrappers in telemetry.c — see
// md/client/device/boot_init_tracking.md's "Proposed transport" section.
//
// Exactly one of telemetry.c / telemetry_boot.c is ever linked into a given
// sample (see Makefile's SAMPLE_TELEMETRY / SAMPLE_TELEMETRY_BOOT), so the
// shared function names here never collide with telemetry.c's.

#include "telemetry.h"

#ifdef RTOS_TELEMETRY

#include "telemetry_frame.h"

void telemetry_report_boot_stage_enter(TelemetryBootStage stage)
{
  uint8_t payload[5] = { (uint8_t)stage, 0, 0, 0, 0 };   // reserved bytes always 0 — see telemetry.h
  telemetry_send_framed(PKT_BOOT_STAGE_ENTER, payload, sizeof(payload));
}

void telemetry_report_boot_milestone(TelemetryBootMilestoneId id, TelemetryBootStage stage, uint32_t counter)
{
  uint8_t payload[6];
  payload[0] = (uint8_t)id;
  payload[1] = (uint8_t)stage;
  payload[2] = (uint8_t)(counter >> 24);
  payload[3] = (uint8_t)(counter >> 16);
  payload[4] = (uint8_t)(counter >> 8);
  payload[5] = (uint8_t)counter;
  telemetry_send_framed(PKT_BOOT_MILESTONE, payload, sizeof(payload));
}

void telemetry_report_boot_info_image_header(TelemetryBootStage stage, uint32_t version_num, uint32_t fw_length,
                                              uint32_t expected_crc, uint32_t actual_crc, uint8_t crc_ok)
{
  uint8_t payload[19];
  payload[0] = (uint8_t)BOOT_INFO_IMAGE_HEADER;
  payload[1] = (uint8_t)stage;
  payload[2] = (uint8_t)(version_num >> 24);
  payload[3] = (uint8_t)(version_num >> 16);
  payload[4] = (uint8_t)(version_num >> 8);
  payload[5] = (uint8_t)version_num;
  payload[6] = (uint8_t)(fw_length >> 24);
  payload[7] = (uint8_t)(fw_length >> 16);
  payload[8] = (uint8_t)(fw_length >> 8);
  payload[9] = (uint8_t)fw_length;
  payload[10] = (uint8_t)(expected_crc >> 24);
  payload[11] = (uint8_t)(expected_crc >> 16);
  payload[12] = (uint8_t)(expected_crc >> 8);
  payload[13] = (uint8_t)expected_crc;
  payload[14] = (uint8_t)(actual_crc >> 24);
  payload[15] = (uint8_t)(actual_crc >> 16);
  payload[16] = (uint8_t)(actual_crc >> 8);
  payload[17] = (uint8_t)actual_crc;
  payload[18] = crc_ok;
  telemetry_send_framed(PKT_BOOT_INFO, payload, sizeof(payload));
}

void telemetry_report_boot_info_slot_state(uint8_t active_app_slot, uint8_t app_slot_trial, uint32_t trial_boot_count,
                                            uint32_t wdt_reset_count, int32_t wdt_reset_tolerance,
                                            uint8_t wdt_reset_policy, uint8_t wdt_reset_reason)
{
  uint8_t payload[17];
  payload[0] = (uint8_t)BOOT_INFO_SLOT_STATE;
  payload[1] = active_app_slot;
  payload[2] = app_slot_trial;
  payload[3] = (uint8_t)(trial_boot_count >> 24);
  payload[4] = (uint8_t)(trial_boot_count >> 16);
  payload[5] = (uint8_t)(trial_boot_count >> 8);
  payload[6] = (uint8_t)trial_boot_count;
  payload[7] = (uint8_t)(wdt_reset_count >> 24);
  payload[8] = (uint8_t)(wdt_reset_count >> 16);
  payload[9] = (uint8_t)(wdt_reset_count >> 8);
  payload[10] = (uint8_t)wdt_reset_count;
  payload[11] = (uint8_t)((uint32_t)wdt_reset_tolerance >> 24);
  payload[12] = (uint8_t)((uint32_t)wdt_reset_tolerance >> 16);
  payload[13] = (uint8_t)((uint32_t)wdt_reset_tolerance >> 8);
  payload[14] = (uint8_t)(uint32_t)wdt_reset_tolerance;
  payload[15] = wdt_reset_policy;
  payload[16] = wdt_reset_reason;
  telemetry_send_framed(PKT_BOOT_INFO, payload, sizeof(payload));
}

void telemetry_report_boot_info_boot_flags(uint8_t reset_reason, uint8_t dfu_requested, uint8_t fw_crc_ok)
{
  uint8_t payload[4] = { (uint8_t)BOOT_INFO_BOOT_FLAGS, reset_reason, dfu_requested, fw_crc_ok };
  telemetry_send_framed(PKT_BOOT_INFO, payload, sizeof(payload));
}

void telemetry_report_dfu_event(TelemetryDfuEventPhase phase, uint8_t target, uint32_t extra)
{
  uint8_t payload[6];
  payload[0] = (uint8_t)phase;
  payload[1] = target;
  payload[2] = (uint8_t)(extra >> 24);
  payload[3] = (uint8_t)(extra >> 16);
  payload[4] = (uint8_t)(extra >> 8);
  payload[5] = (uint8_t)extra;
  telemetry_send_framed(PKT_DFU_EVENT, payload, sizeof(payload));
}

#endif // RTOS_TELEMETRY
