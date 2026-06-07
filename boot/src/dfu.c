#include "dfu.h"

#include <stdbool.h>

#include "boot_flags.h"
#include "emmc.h"
#include "uart.h"

static uint32_t sectors_written;
static uint8_t current_sector[SECTOR_SIZE];
static uint32_t current_sector_counter;
static bool unwritten_sector;
static bool started;

typedef struct StartPacket {
  uint16_t version_num;
  uint16_t app_length;
  uint32_t crc;
} StartPacket;

static DFU_Packet dfu_packet_receive()
{
  DFU_Packet packet;
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
      if (byte < NUM_CMDS) {
        packet.CMD = byte;
      }
      else {
        state = PACKET_STATE_ERROR;
      }
      break;

    case PACKET_STATE_LENGTH:
      uint8_t byte_lsb = uart_rx();
      packet.LEN = (uint16_t)((uint16_t)(byte << 8U) | byte_lsb);
      state = PACKET_STATE_PAYLOAD;
      break;

    case PACKET_STATE_PAYLOAD:
      uint16_t remaining = 1;
      packet.DATA[0] = byte;
      while (remaining < packet.LEN) {
        packet.DATA[remaining] = uart_rx();
        remaining++;
      }
      state = PACKET_STATE_CRC;
      break;

    case PACKET_STATE_CRC:
      uint8_t crc_b2 = uart_rx();
      uint8_t crc_b3 = uart_rx();
      uint8_t crc_b4 = uart_rx();

      uint32_t crc = ((uint32_t)(byte << 24U) | (uint32_t)(crc_b2 << 16U) | (uint32_t)(crc_b3 << 8U) | (crc_b4));

      // Validate crc
      state = PACKET_STATE_END;
      break;

    case PACKET_STATE_END:
      if (byte == 0xBB) {
        state = PACKET_STATE_SUCCESS;
      }
      break;

    case PACKET_STATE_ERROR:
      packet.CMD = 0;
      packet.CRC = 0;
      packet.LEN = 0;
      state = PACKET_STATE_START;
      break;

    default:
      break;
    }
  }

  return packet;
}

StatusCode dfu_init()
{
  sectors_written = 0;
  current_sector_counter = 0;
  unwritten_sector = false;
  started = false;
}

StatusCode dfu_receive()
{
  DFU_state state = DFU_STATE_START;

  bool flags_crc_ok_set = false;
  uint32_t img_expected_crc;
  uint32_t img_actual_crc;

  while (state != DFU_STATE_DONE && state != DFU_STATE_ABORT) {
    DFU_Packet packet = dfu_packet_receive();
    switch (packet.CMD) {
    case CMD_REQ:
      // Send version, slot info
      break;

    case CMD_START:
      if (!started) {
        if (packet.LEN != 8U) {
          state = DFU_STATE_ABORT;
          break;
        }
        state = DFU_STATE_RECIEVE_DATA;
        for (uint32_t i = 0; i < 8U; i++) {
          current_sector[i] = packet.DATA[i];
        }

        current_sector_counter = 8U;

        const StartPacket *start_pkt = (const StartPacket *)packet.DATA;
        img_expected_crc = start_pkt->crc;

        started = true;
      }
      else {
        state = DFU_STATE_ABORT;
      }
      break;

    case CMD_DATA:
      // our packet length must be a multiple of 32 bytes
      if (packet.LEN % 4 != 0) {
        state = DFU_STATE_ABORT;
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
          emmcWriteBlocks(EMMC_SECTOR_APP + sectors_written, current_sector, 1U);

          // Copy over the overflow to the start of the buffer
          for (uint32_t i = 0; i < diff; i++) {
            current_sector[i] = packet.DATA[packet.LEN - diff + i];
          }

          // Increment sectors written and mark that we have no unwritten sectors
          sectors_written++;
          if (diff == 0) {
            unwritten_sector = false;
          }
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
        }

        // compute img_actual_crc
        // ACK
      }

      break;

    case CMD_ABORT:
      state = DFU_STATE_ABORT;
      break;

    case CMD_FINISH:
      // check if we need to write one more sector
      if (unwritten_sector) {
        // Pad the final sector
        for (uint32_t i = current_sector_counter; i < SECTOR_SIZE; i++) {
          current_sector[i] = 0;
        }

        // write it
        emmcWriteBlocks(EMMC_SECTOR_APP + sectors_written, current_sector, 1U);
      }
      state = DFU_STATE_DONE;
      break;

    case CMD_GET_STATUS:
      // send status
      break;

    case CMD_RECOVER:
      // recovery routine
      break;
    }
  }

  if (state == DFU_STATE_DONE) {
    if (img_expected_crc == img_actual_crc) {
      boot_flags.fw_crc_ok = 1;
      return E_OK;
    }
    else {
      return E_CORRUPTED;
    }
  }
  else if (state == DFU_STATE_ABORT) {
    return E_ABORTED;
  }
}