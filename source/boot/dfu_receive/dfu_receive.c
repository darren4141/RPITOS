#include "dfu_receive.h"

#include <stdbool.h>

#include "boot.h"
#include "boot_flags.h"
#include "crc.h"
#include "dfu_trigger.h"
#include "emmc.h"
#include "telemetry.h"
#include "uart.h"
#include "watchdog.h"

#define DFU_ACK               0x06
#define DFU_NACK              0x15

#define DFU_PACKET_TIMEOUT_MS 500U

static uint32_t sectors_written;
static uint8_t current_sector[SECTOR_SIZE];
static uint32_t current_sector_counter;
static bool unwritten_sector;
static bool started;
static bool is_self_update;
static uint32_t dfu_base_sector;   // header sector of the slot being written

#ifdef RTOS_TELEMETRY
// Which image this session is writing — DFU_TARGET_UNKNOWN until CMD_START/
// _SELF_UPDATE pins it, then held for the rest of the session (including
// telemetry_report_dfu_event(DFU_EVT_ABORTED, ...) calls after that point).
static uint8_t dfu_telemetry_target;
// Last percent-of-img_length boundary a DFU_EVT_DATA_PROGRESS was sent at —
// see the throttling note on telemetry_report_dfu_event() call sites below.
static uint32_t dfu_telemetry_last_pct;
#endif

// Helper function to receive one dfu packet at a time and pack it into a struct. also returns timeout error code
static StatusCode dfu_packet_receive(DFU_Packet *packet)
{
  PacketState state = PACKET_STATE_START;
  while (state != PACKET_STATE_SUCCESS) {
    uint8_t byte = uart_rx();
    switch (state) {

    case PACKET_STATE_START:
      if (byte == 0xAA) {
        state = PACKET_STATE_CMD;
      }
      break;

    case PACKET_STATE_CMD:
      if ((byte >= CMD_REQ) && (byte < NUM_CMDS)) {
        packet->CMD = byte;
        state = PACKET_STATE_LENGTH;
      }
      else {
        state = PACKET_STATE_ERROR;
      }
      break;

    case PACKET_STATE_LENGTH: {
      uint8_t byte_lsb;
      if (uart_rx_timed(&byte_lsb, DFU_PACKET_TIMEOUT_MS) != E_OK) {
        return E_TIMED_OUT;
      }
      packet->LEN = (uint16_t)((uint16_t)(byte << 8U) | byte_lsb);
      state = PACKET_STATE_PAYLOAD;
      break;
    }

    case PACKET_STATE_PAYLOAD: {
      uint16_t remaining = 1;
      packet->DATA[0] = byte;
      while (remaining < packet->LEN) {
        if (uart_rx_timed(&packet->DATA[remaining], DFU_PACKET_TIMEOUT_MS) != E_OK) {
          return E_TIMED_OUT;
        }
        remaining++;
      }
      state = PACKET_STATE_CRC;
      break;
    }

    case PACKET_STATE_CRC: {
      uint8_t crc_b2, crc_b3, crc_b4;
      if (uart_rx_timed(&crc_b2, DFU_PACKET_TIMEOUT_MS) != E_OK) {
        return E_TIMED_OUT;
      }
      if (uart_rx_timed(&crc_b3, DFU_PACKET_TIMEOUT_MS) != E_OK) {
        return E_TIMED_OUT;
      }
      if (uart_rx_timed(&crc_b4, DFU_PACKET_TIMEOUT_MS) != E_OK) {
        return E_TIMED_OUT;
      }

      packet->CRC = ((uint32_t)(byte << 24U) | (uint32_t)(crc_b2 << 16U) | (uint32_t)(crc_b3 << 8U) | crc_b4);
      state = PACKET_STATE_END;
      break;
    }

    case PACKET_STATE_END:
      if (byte == 0xBB) {
        state = PACKET_STATE_SUCCESS;
      }
      break;

    case PACKET_STATE_ERROR:
      packet->CMD = 0;
      packet->CRC = 0;
      packet->LEN = 0;
      state = PACKET_STATE_START;
      break;

    default:
      break;
    }
  }
  return E_OK;
}

StatusCode dfu_init()
{
  sectors_written = 0;
  current_sector_counter = 0;
  unwritten_sector = false;
  started = false;
  is_self_update = false;
  dfu_base_sector = 0;
#ifdef RTOS_TELEMETRY
  dfu_telemetry_target = DFU_TARGET_UNKNOWN;
  dfu_telemetry_last_pct = 0;
#endif

  return E_OK;
}

StatusCode dfu_receive()
{
  dfu_init();
  dfu_trigger_reset();

#ifdef RTOS_TELEMETRY
  telemetry_report_dfu_event(DFU_EVT_WAITING_TRIGGER, DFU_TARGET_UNKNOWN, 0);
#endif

  // Wait for the DFU trigger key
  while (1) {
    uint8_t byte = uart_rx();
    if (dfu_trigger_feed(byte)) {
      uart_tx_raw(DFU_ACK);
      dfu_trigger_reset();
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_TRIGGER_MATCHED, DFU_TARGET_UNKNOWN, 0);
#endif
      break;
    }
  }

  DfuState state = DFU_STATE_START;

  uint32_t img_expected_crc;
  uint32_t img_length = 0;
  uint32_t bytes_hashed = 0;
  CRC32 crc_ctx;

  while (state != DFU_STATE_DONE && state != DFU_STATE_ABORT) {
    DFU_Packet packet;
    if (dfu_packet_receive(&packet) != E_OK) {
      // Timed out mid-transfer. We were writing the *inactive* slot, so the
      // active slot is untouched - just abandon without committing the flip.
      uart_print("DFU: session timed out\r\n");
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_ABORTED, dfu_telemetry_target, DFU_ABORT_TIMEOUT);
#endif
      return E_TIMED_OUT;
    }
    switch (packet.CMD) {
    case CMD_REQ:
      // Send version, slot info
      break;

    case CMD_START:
      if (!started) {
        if (packet.LEN != sizeof(StartPacket)) {
          uart_tx_raw(DFU_NACK);
          state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
          telemetry_report_dfu_event(DFU_EVT_ABORTED, DFU_TARGET_UNKNOWN, DFU_ABORT_BAD_START_LEN);
#endif
          break;
        }
        state = DFU_STATE_RECIEVE_DATA;
        for (uint32_t i = 0; i < sizeof(StartPacket); i++) {
          current_sector[i] = packet.DATA[i];
        }

        const StartPacket *start_pkt = (const StartPacket *)packet.DATA;
        img_expected_crc = start_pkt->crc;
        img_length = start_pkt->fw_length;
        crc32_start(&crc_ctx);

        // A/B: write the inactive slot. The active (running) slot is left
        // untouched and stays valid until we validate and flip at CMD_FINISH.
        dfu_base_sector = APP_SLOT_TO_SECTOR(APP_SLOT_OTHER(wdt_meta.active_app_slot));
        emmc_write_blocks(dfu_base_sector, current_sector, 1U);
        sectors_written = 1;

        is_self_update = false;
        started = true;
#ifdef RTOS_TELEMETRY
        dfu_telemetry_target = DFU_TARGET_APP;
        telemetry_report_dfu_event(DFU_EVT_START_RECEIVED, DFU_TARGET_APP, img_length);
#endif
        uart_tx_raw(DFU_ACK);
      }
      else {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
        telemetry_report_dfu_event(DFU_EVT_ABORTED, dfu_telemetry_target, DFU_ABORT_ALREADY_STARTED);
#endif
      }
      break;

    case CMD_START_SELF_UPDATE:
      if (!started) {
        if (packet.LEN != sizeof(StartPacket)) {
          uart_tx_raw(DFU_NACK);
          state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
          telemetry_report_dfu_event(DFU_EVT_ABORTED, DFU_TARGET_UNKNOWN, DFU_ABORT_BAD_START_LEN);
#endif
          break;
        }
        state = DFU_STATE_RECIEVE_DATA;
        for (uint32_t i = 0; i < sizeof(StartPacket); i++) {
          current_sector[i] = packet.DATA[i];
        }

        const StartPacket *start_pkt = (const StartPacket *)packet.DATA;
        img_expected_crc = start_pkt->crc;
        img_length = start_pkt->fw_length;
        crc32_start(&crc_ctx);

        dfu_base_sector = EMMC_SECTOR_BOOTLOADER;
        emmc_write_blocks(dfu_base_sector, current_sector, 1U);
        sectors_written = 1;

        is_self_update = true;
        started = true;
#ifdef RTOS_TELEMETRY
        dfu_telemetry_target = DFU_TARGET_BOOTLOADER;
        telemetry_report_dfu_event(DFU_EVT_START_RECEIVED, DFU_TARGET_BOOTLOADER, img_length);
#endif
        uart_tx_raw(DFU_ACK);
      }
      else {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
        telemetry_report_dfu_event(DFU_EVT_ABORTED, dfu_telemetry_target, DFU_ABORT_ALREADY_STARTED);
#endif
      }
      break;

    case CMD_DATA:
      // our packet length must be a multiple of 4 bytes
      if (packet.LEN % 4 != 0) {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
        telemetry_report_dfu_event(DFU_EVT_ABORTED, dfu_telemetry_target, DFU_ABORT_BAD_DATA_LEN);
#endif
        break;
      }
      if (state == DFU_STATE_RECIEVE_DATA) {
        uint32_t base_sector = dfu_base_sector;

        if (current_sector_counter + packet.LEN >= SECTOR_SIZE) {
          uint32_t diff = current_sector_counter + packet.LEN - SECTOR_SIZE;

          for (uint32_t i = 0; i < packet.LEN - diff; i++) {
            current_sector[current_sector_counter + i] = packet.DATA[i];
          }

          emmc_write_blocks(base_sector + sectors_written, current_sector, 1U);

          for (uint32_t i = 0; i < diff; i++) {
            current_sector[i] = packet.DATA[packet.LEN - diff + i];
          }

          sectors_written++;
          unwritten_sector = (diff != 0);
          current_sector_counter = diff;
        }
        else {
          if (!unwritten_sector) {
            unwritten_sector = true;
          }

          for (uint32_t i = 0; i < packet.LEN; i++) {
            current_sector[current_sector_counter + i] = packet.DATA[i];
          }

          current_sector_counter += packet.LEN;
        }

        uint32_t to_hash = packet.LEN;
        if (bytes_hashed + to_hash > img_length) {
          to_hash = (img_length > bytes_hashed) ? img_length - bytes_hashed : 0;
        }
        if (to_hash > 0) {
          crc32_update(&crc_ctx, packet.DATA, to_hash);
          bytes_hashed += to_hash;
        }

#ifdef RTOS_TELEMETRY
        // Throttled to ~1%-of-img_length steps (~100 packets/transfer
        // regardless of image size) — DFU is single-threaded and ACK-gated,
        // so an event per 256-byte CMD_DATA chunk (up to ~32k for an 8MB app)
        // would add that many blocking UART5 writes onto the flashing hot
        // path. See md/client/device/boot_init_tracking.md.
        if (img_length > 0) {
          uint32_t pct = (bytes_hashed * 100U) / img_length;
          if (pct > dfu_telemetry_last_pct) {
            dfu_telemetry_last_pct = pct;
            telemetry_report_dfu_event(DFU_EVT_DATA_PROGRESS, dfu_telemetry_target, bytes_hashed);
          }
        }
#endif

        uart_tx_raw(DFU_ACK);
      }

      break;

    case CMD_ABORT:
      uart_tx_raw(DFU_NACK);
      state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_ABORTED, dfu_telemetry_target, DFU_ABORT_HOST_ABORT);
#endif
      break;

    case CMD_FINISH: {
      uint32_t img_actual_crc = crc32_finish(&crc_ctx);
      uart_printf("DFU CRC | Expected: 0x%08X | Actual: 0x%08X\r\n", img_expected_crc, img_actual_crc);
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_CRC_CHECK_RESULT, dfu_telemetry_target,
                                  (img_actual_crc == img_expected_crc) ? 1U : 0U);
#endif
      if (img_actual_crc != img_expected_crc) {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
#ifdef RTOS_TELEMETRY
        telemetry_report_dfu_event(DFU_EVT_ABORTED, dfu_telemetry_target, DFU_ABORT_CRC_MISMATCH);
#endif
        break;
      }

      if (unwritten_sector) {
        for (uint32_t i = current_sector_counter; i < SECTOR_SIZE; i++) {
          current_sector[i] = 0;
        }
        emmc_write_blocks(dfu_base_sector + sectors_written, current_sector, 1U);
      }

      if (is_self_update) {
        uart_tx_raw(DFU_ACK);
        uart_print("DFU: bootloader update complete, resetting\r\n");
#ifdef RTOS_TELEMETRY
        // extra=0 — "new active_app_slot" doesn't apply to a bootloader self-update.
        telemetry_report_dfu_event(DFU_EVT_COMMITTED, DFU_TARGET_BOOTLOADER, 0);
#endif
        uart_deinit();
        watchdog_trigger_reset();
        // never reached
      }

      // App update landed in the inactive slot. The in-flight CRC only proves
      // the transfer was intact, not that the bytes actually committed to eMMC
      // - so validate by reading the slot back before flipping to it.
      uint32_t new_slot = APP_SLOT_OTHER(wdt_meta.active_app_slot);
      bool app_valid = (boot_validate_app(APP_SLOT_TO_SECTOR(new_slot)) == E_OK);
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_READBACK_VALIDATE, DFU_TARGET_APP, app_valid ? 1U : 0U);
#endif
      if (!app_valid) {
        uart_print("DFU: new slot failed read-back validation, not committing\r\n");
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;   // active slot untouched - nothing to roll back
#ifdef RTOS_TELEMETRY
        telemetry_report_dfu_event(DFU_EVT_ABORTED, DFU_TARGET_APP, DFU_ABORT_READBACK_FAILED);
#endif
        break;
      }

      // Enforce that the image was built with the standard app startup (and so
      // links the DFU-trigger support). The marker sits at a fixed offset in
      // the app's vector-table region, i.e. the first firmware sector. Read it
      // back from eMMC rather than trusting the in-flight stream.
      uint8_t marker_sector[SECTOR_SIZE] __attribute__((aligned(4)));
      bool marker_ok = (emmc_read_blocks(APP_SLOT_TO_SECTOR(new_slot) + 1U, marker_sector, 1U) == E_OK)
                        && (*(const uint32_t *)&marker_sector[DFU_APP_MARKER_OFFSET] == DFU_APP_MAGIC);
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_MARKER_CHECK, DFU_TARGET_APP, marker_ok ? 1U : 0U);
#endif
      if (!marker_ok) {
        uart_print("DFU: image missing DFU-support marker, refusing to commit\r\n");
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;   // active slot untouched — nothing to roll back
#ifdef RTOS_TELEMETRY
        telemetry_report_dfu_event(DFU_EVT_ABORTED, DFU_TARGET_APP, DFU_ABORT_MARKER_MISSING);
#endif
        break;
      }

      // Commit: make the new slot active but mark it on-trial. The app must call
      // wdt_meta_confirm_slot() once healthy, or the bootloader rolls back.
      wdt_meta.active_app_slot = new_slot;
      wdt_meta.app_slot_trial = 1U;
      wdt_meta.trial_boot_count = 0U;
      wdt_meta_write();
      boot_flags.fw_crc_ok = 1U;

      uart_tx_raw(DFU_ACK);
      uart_printf("DFU: slot %s written & validated, now active (trial)\r\n",
                  APP_SLOT_LETTER(new_slot));
#ifdef RTOS_TELEMETRY
      telemetry_report_dfu_event(DFU_EVT_COMMITTED, DFU_TARGET_APP, (new_slot == APP_SLOT_B) ? 1U : 0U);
#endif

      state = DFU_STATE_DONE;
      break;
    }

    case CMD_GET_STATUS:
      // send status
      break;

    case CMD_RECOVER:
      // recovery routine
      break;
    }
  }

  if (state == DFU_STATE_DONE) {
    return E_OK;
  }

  // Aborted while writing the inactive slot — active slot never changed.
  return E_ABORTED;
}
