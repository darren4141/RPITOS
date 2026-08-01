#include "queue.h"

#include <stddef.h>

#include "heap.h"
#include "interrupts.h"

StatusCode queue_init(Queue *q, uint32_t capacity, uint32_t item_size)
{
  q->capacity = capacity;
  q->item_size = item_size;
  q->head = 0;
  q->tail = 0;
  q->buf = heap_malloc(capacity * item_size);

  if (q->buf == NULL) {
    return E_OUT_OF_MEM;
  }

  semaphore_init(&q->data_available, capacity, 0);
  semaphore_init(&q->space_available, capacity, capacity);
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