#include "dfu_receive.h"

#include <stdbool.h>

#include "boot_flags.h"
#include "crc.h"
#include "dfu_trigger.h"
#include "emmc.h"
#include "uart.h"

#define DFU_ACK               0x06
#define DFU_NACK              0x15

#define DFU_PACKET_TIMEOUT_MS 500U

static uint32_t sectors_written;
static uint8_t current_sector[SECTOR_SIZE];
static uint32_t current_sector_counter;
static bool unwritten_sector;
static bool started;

static StatusCode dfu_packet_receive(DFU_Packet *packet)
{
  Packet_State state = PACKET_STATE_START;
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

  return E_OK;
}

StatusCode dfu_receive()
{
  dfu_init();
  dfu_trigger_reset();
  while (1) {
    uint8_t byte = uart_rx();
    if (dfu_trigger_feed(byte)) {
      uart_tx_raw(DFU_ACK);
      dfu_trigger_reset();
      break;
    }
  }

  DFU_State state = DFU_STATE_START;

  bool flags_crc_ok_set = false;
  uint32_t img_expected_crc;
  uint32_t app_length = 0;
  uint32_t bytes_hashed = 0;
  CRC32_t crc_ctx;

  while (state != DFU_STATE_DONE && state != DFU_STATE_ABORT) {
    DFU_Packet packet;
    if (dfu_packet_receive(&packet) != E_OK) {
      uart_print("DFU: session timed out\r\n");
      return E_TIMED_OUT;
    }
    switch (packet.CMD) {
    case CMD_REQ:
      // Send version, slot info
      break;

    case CMD_START:
      if (!started) {
        if (packet.LEN != 8U) {
          uart_tx_raw(DFU_NACK);
          state = DFU_STATE_ABORT;
          break;
        }
        state = DFU_STATE_RECIEVE_DATA;
        for (uint32_t i = 0; i < 8U; i++) {
          current_sector[i] = packet.DATA[i];
        }

        const StartPacket *start_pkt = (const StartPacket *)packet.DATA;
        img_expected_crc = start_pkt->crc;
        app_length = start_pkt->app_length;
        crc32_start(&crc_ctx);

        emmc_write_blocks(EMMC_SECTOR_APP, current_sector, 1U);
        sectors_written = 1;

        started = true;
        uart_tx_raw(DFU_ACK);
      }
      else {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
      }
      break;

    case CMD_DATA:
      // our packet length must be a multiple of 32 bytes
      if (packet.LEN % 4 != 0) {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
        break;
      }
      if (state == DFU_STATE_RECIEVE_DATA) {
        // Set the crc_ok flag to 0 so we know the data in eMMC is now bad (mid-write)
        if (!flags_crc_ok_set) {
          flags_crc_ok_set = true;
          boot_flags.fw_crc_ok = 0;
        }

        // If this data write meets or exceeds sector size, we must write a sector
        if (current_sector_counter + packet.LEN >= SECTOR_SIZE) {
          // Calculate the overflow (hopefully zero)
          uint32_t diff = current_sector_counter + packet.LEN - SECTOR_SIZE;

          // Copy over what we need
          for (uint32_t i = 0; i < packet.LEN - diff; i++) {
            current_sector[current_sector_counter + i] = packet.DATA[i];
          }

          // Write the buffer
          emmc_write_blocks(EMMC_SECTOR_APP + sectors_written, current_sector, 1U);

          // Copy over the overflow to the start of the buffer
          for (uint32_t i = 0; i < diff; i++) {
            current_sector[i] = packet.DATA[packet.LEN - diff + i];
          }

          // Increment sectors written and mark that we have no unwritten sectors
          sectors_written++;
          if (diff == 0) {
            unwritten_sector = false;
          }
          else {
            unwritten_sector = true;
          }

          // Update current_sector_counter

          current_sector_counter = diff;
        }
        else {
          // Set unwritten sector if we have not
          if (!unwritten_sector) {
            unwritten_sector = true;
          }

          // Copy over data
          for (uint32_t i = 0; i < packet.LEN; i++) {
            current_sector[current_sector_counter + i] = packet.DATA[i];
          }

          current_sector_counter += packet.LEN;
        }

        uint32_t to_hash = packet.LEN;
        if (bytes_hashed + to_hash > app_length) {
          to_hash = (app_length > bytes_hashed) ? app_length - bytes_hashed : 0;
        }
        if (to_hash > 0) {
          crc32_update(&crc_ctx, packet.DATA, to_hash);
          bytes_hashed += to_hash;
        }
        uart_tx_raw(DFU_ACK);
      }

      break;

    case CMD_ABORT:
      uart_tx_raw(DFU_NACK);
      state = DFU_STATE_ABORT;
      break;

    case CMD_FINISH: {
      uint32_t img_actual_crc = crc32_finish(&crc_ctx);
      uart_printf("DFU CRC | Expected: 0x%08X | Actual: 0x%08X\r\n", img_expected_crc, img_actual_crc);
      if (img_actual_crc != img_expected_crc) {
        uart_tx_raw(DFU_NACK);
        state = DFU_STATE_ABORT;
        break;
      }

      // check if we need to write one more sector
      if (unwritten_sector) {
        // Pad the final sector
        for (uint32_t i = current_sector_counter; i < SECTOR_SIZE; i++) {
          current_sector[i] = 0;
        }

        // write it
        emmc_write_blocks(EMMC_SECTOR_APP + sectors_written, current_sector, 1U);
      }
      uart_tx_raw(DFU_ACK);
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
    boot_flags.fw_crc_ok = 1;
    return E_OK;
  }

  return E_ABORTED;
}
