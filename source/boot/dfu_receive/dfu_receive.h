#ifndef DFU_RECEIVE_H
#define DFU_RECEIVE_H

#include <stdint.h>

#include "status.h"

#define MAX_DATA_SIZE_BYTES 256U

#define PACKET_SOF          0xAA

typedef enum {
  CMD_REQ              = 0x01,
  CMD_START            = 0x02,
  CMD_START_SELF_UPDATE = 0x03,
  CMD_DATA             = 0x04,
  CMD_FINISH           = 0x05,
  CMD_ABORT            = 0x06,
  CMD_GET_STATUS       = 0x07,
  CMD_RECOVER          = 0x08,
  NUM_CMDS
} Commands;

typedef enum {
  PACKET_STATE_START,
  PACKET_STATE_CMD,
  PACKET_STATE_LENGTH,
  PACKET_STATE_PAYLOAD,
  PACKET_STATE_CRC,
  PACKET_STATE_END,
  PACKET_STATE_SUCCESS,
  PACKET_STATE_ERROR,
} Packet_State;

typedef enum {
  DFU_STATE_START,
  DFU_STATE_RECIEVE_DATA,
  DFU_STATE_DONE,
  DFU_STATE_ABORT,
} DFU_State;

typedef struct DFU_Packet {
  uint8_t CMD;
  uint16_t LEN;
  uint8_t DATA[MAX_DATA_SIZE_BYTES];
  uint32_t CRC;
} DFU_Packet;

// Header sector written to eMMC before the binary for both app and bootloader.
// All fields are uint32_t so the struct is naturally word-aligned — no packing
// needed, no unaligned-access hazards on bare-metal device memory.
typedef struct StartPacket {
  uint32_t version_num;
  uint32_t fw_length;
  uint32_t crc;
} StartPacket;

StatusCode dfu_init();
StatusCode dfu_receive();

#endif
