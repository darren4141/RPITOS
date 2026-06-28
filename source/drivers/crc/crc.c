#include "crc.h"

StatusCode crc32_init()
{
  uint32_t isar5;
  asm volatile ("mrc p15, 0, %0, c0, c2, 5" : "=r" (isar5));
  return ((isar5 >> 16) & 0xF) != 0 ? E_OK : E_NOTSUPP;
}

static void crc32_hw_update(uint32_t *crc, const uint8_t *data, size_t len)
{
  while (len >= 4) {
    uint32_t val = (uint32_t)data[0] | ((uint32_t)data[1] << 8)
                 | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    asm volatile ("crc32w %0, %1, %2" : "=r" (*crc) : "r" (*crc), "r" (val));
    data += 4;
    len -= 4;
  }
  if (len >= 2) {
    uint16_t val = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    asm volatile ("crc32h %0, %1, %2" : "=r" (*crc) : "r" (*crc), "r" ((uint32_t)val));
    data += 2;
    len -= 2;
  }
  if (len >= 1) {
    asm volatile ("crc32b %0, %1, %2" : "=r" (*crc) : "r" (*crc), "r" ((uint32_t)*data));
  }
}

void crc32_start(CRC32_t *ctx)
{
  ctx->crc = 0xFFFFFFFF;
}

void crc32_update(CRC32_t *ctx, const uint8_t *data, size_t len)
{
  crc32_hw_update(&ctx->crc, data, len);
}

void crc32_update_byte(CRC32_t *ctx, uint8_t byte)
{
  crc32_update(ctx, &byte, 1);
}

uint32_t crc32_finish(CRC32_t *ctx)
{
  return ctx->crc ^ 0xFFFFFFFF;
}

int crc32_verify(CRC32_t *ctx, uint32_t expected)
{
  return crc32_finish(ctx) == expected;
}