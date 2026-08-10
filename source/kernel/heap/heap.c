#include "heap.h"

#include <stddef.h>

#include "interrupts.h"
#include "spinlock.h"

#define HEAP_SIZE_BYTES 262144   // 256KB

static uint8_t heap[HEAP_SIZE_BYTES];
static uint32_t heap_offset = 0;

// Guards heap_offset: task_create() on any core calls heap_malloc()
static Spinlock heap_lock;

void *heap_malloc(uint32_t size)
{
  size = (size + 3) & ~0b11;

  // enter_critical() is required alongside the spinlock,
  uint32_t cpsr = enter_critical();
  spinlock_acquire(&heap_lock);

  if (heap_offset + size > HEAP_SIZE_BYTES) {
    spinlock_release(&heap_lock);
    exit_critical(cpsr);
    return NULL;
  }

  void *block_start = &heap[heap_offset];
  heap_offset += size;

  spinlock_release(&heap_lock);
  exit_critical(cpsr);
  return block_start;
}
