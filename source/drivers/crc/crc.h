#ifndef CRC_H
#define CRC_H

#include "status.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t crc;         // running CRC value (internal, pre-finalize)
} CRC32;

/**
 * @brief Check whether the CPU supports the ARMv8 CRC32 instructions this driver relies on (via ID_ISAR5).
 * @return E_NOTSUPP if the hardware CRC32 extension is absent.
 */
StatusCode crc32_init();

/**
 * @brief Reset a CRC32 context to its initial value, ready for crc32_update().
 */
void crc32_start(CRC32 *ctx);

/**
 * @brief Fold len bytes of data into a running CRC32.
 */
void crc32_update(CRC32 *ctx, const uint8_t *data, size_t len);

/**
 * @brief Fold a single byte into a running CRC32.
 */
void crc32_update_byte(CRC32 *ctx, uint8_t byte);

/**
 * @brief Finalize a CRC32 context and return the checksum.
 */
uint32_t crc32_finish(CRC32 *ctx);

/**
 * @brief Finalize a CRC32 context and compare it against an expected value.
 * @return Non-zero if they match.
 */
int crc32_verify(CRC32 *ctx, uint32_t expected);

#endif
