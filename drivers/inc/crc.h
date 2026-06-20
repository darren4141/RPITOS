#ifndef CRC_H
#define CRC_H

#include "status.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t crc;         // running CRC value (internal, pre-finalize)
} CRC32_t;


StatusCode crc32_init();
void crc32_start(CRC32_t *ctx);
void crc32_update(CRC32_t *ctx, const uint8_t *data, size_t len);
void crc32_update_byte(CRC32_t *ctx, uint8_t byte);
uint32_t crc32_finish(CRC32_t *ctx);
int crc32_verify(CRC32_t *ctx, uint32_t expected);


#endif
