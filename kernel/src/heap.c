#include "heap.h"

#include <stddef.h>

#define HEAP_SIZE_BYTES 16384

static uint8_t heap[HEAP_SIZE_BYTES];
static uint32_t heapOffset = 0;

void *heap_malloc(uint32_t size)
{
  size = (size + 3) & ~0b11;

  if (heapOffset + size > HEAP_SIZE_BYTES) {
    return NULL;
  }

  void *blockStart = &heap[heapOffset];
  heapOffset += size;
  return blockStart;
}
