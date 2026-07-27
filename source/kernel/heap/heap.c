#include "heap.h"

#include <stddef.h>

#define HEAP_SIZE_BYTES 262144   // 256KB

static uint8_t heap[HEAP_SIZE_BYTES];
static uint32_t heap_offset = 0;

void *heap_malloc(uint32_t size)
{
  size = (size + 3) & ~0b11;

  if (heap_offset + size > HEAP_SIZE_BYTES) {
    return NULL;
  }

  void *block_start = &heap[heap_offset];
  heap_offset += size;
  return block_start;
}
