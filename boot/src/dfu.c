#include "dfu.h"

#include <stdbool.h>

#include "boot_flags.h"
#include "uart.h"

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
      state = DFU_STATE_RECIEVE_DATA;
      img_expected_crc = ((uint32_t)(packet.DATA[3] << 24U) | (uint32_t)(packet.DATA[2] << 16U) | (uint32_t)(packet.DATA[1] << 8U) | (packet.DATA[0]));
      break;

    case CMD_DATA:
      if (state == DFU_STATE_RECIEVE_DATA) {
        if (!flags_crc_ok_set) {
          flags_crc_ok_set = true;
          boot_flags.fw_crc_ok = 0;
        }
        // copy data to eMMC
        // compute img_actual_crc
        // ACK
      }

      break;

    case CMD_ABORT:
      state = DFU_STATE_ABORT;
      break;

    case CMD_FINISH:
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