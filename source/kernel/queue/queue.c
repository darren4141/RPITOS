#include "queue.h"

#include <stddef.h>

#include "heap.h"
#include "interrupts.h"
#include "telemetry.h"

StatusCode queue_init(Queue *q, uint32_t capacity, uint32_t item_size, const char *name)
{
  q->capacity = capacity;
  q->item_size = item_size;
  q->head = 0;
  q->tail = 0;
  q->buf = heap_malloc(capacity * item_size);

  if (q->buf == NULL) {
    return E_OUT_OF_MEM;
  }

#ifdef RTOS_TELEMETRY
  // Register the queue itself first so its sync_id exists to hand to its two
  // internal semaphores as parent_sync_id. Their own names are short and generic
  // ("space"/"data") — the host combines them with the queue's own (real) name
  // for display, rather than building a concatenated string on-device with no
  // libc string formatting available in this freestanding build.
  q->sync_id = telemetry_register_sync(SYNC_KIND_QUEUE, TELEMETRY_SYNC_ID_NONE, name);
  semaphore_init_with_parent(&q->data_available, capacity, 0, "data", q->sync_id);
  semaphore_init_with_parent(&q->space_available, capacity, capacity, "space", q->sync_id);
#else
  (void)name;
  semaphore_init(&q->data_available, capacity, 0, NULL);
  semaphore_init(&q->space_available, capacity, capacity, NULL);
#endif
  spinlock_init(&q->lock);

  return E_OK;
}

StatusCode queue_send(Queue *q, const void *src, int64_t timeout_ms)
{
  if (semaphore_take(&q->space_available, timeout_ms) != E_OK) {
    return E_TIMED_OUT;
  }

  uint32_t cpsr = enter_critical();
  spinlock_acquire(&q->lock);

  // perform the send

  for (uint32_t i = 0; i < q->item_size; i++) {
    q->buf[(q->head * q->item_size) + i] = ((const uint8_t *)src)[i];
  }

  q->head = (q->head + 1) % q->capacity;

  spinlock_release(&q->lock);
  exit_critical(cpsr);

  semaphore_give(&q->data_available);

  return E_OK;
}

StatusCode queue_recv(Queue *q, void *dst, int64_t timeout_ms)
{
  if (semaphore_take(&q->data_available, timeout_ms) != E_OK) {
    return E_TIMED_OUT;
  }

  uint32_t cpsr = enter_critical();
  spinlock_acquire(&q->lock);

  // perform the receive

  for (uint32_t i = 0; i < q->item_size; i++) {
    ((uint8_t *)dst)[i] = q->buf[(q->tail * q->item_size) + i];
  }

  q->tail = (q->tail + 1) % q->capacity;

  spinlock_release(&q->lock);
  exit_critical(cpsr);

  semaphore_give(&q->space_available);

  return E_OK;
}