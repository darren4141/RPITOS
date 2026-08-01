#include "heap.h"

#include <stddef.h>

#include "interrupts.h"
#include "spinlock.h"

#define HEAP_SIZE_BYTES 262144   // 256KB

static uint8_t heap[HEAP_SIZE_BYTES];
static uint32_t heap_offset = 0;

// Guards heap_offset: task_create() on any core calls heap_malloc() for a
// new task's stack, and the .bss-zeroed initial state (both fields 0) is
// already the unlocked state, so no explicit init call is needed.
static Spinlock heap_lock;

void *heap_malloc(uint32_t size)
{
  size = (size + 3) & ~0b11;

  // enter_critical() is required alongside the spinlock, not redundant with
  // it: the Bakery lock tracks one ticket per core, not per task. Without
  // masking IRQs here, a timer tick could preempt this core mid-critical-
  // section and hand off to a different task on the SAME core that also
  // calls heap_malloc() — that second call's spinlock_acquire() would
  // overwrite this core's ticket slot and enter concurrently, racing on
  // heap_offset. See spinlock/docs.md's lock-order note.
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
