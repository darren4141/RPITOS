#ifndef QUEUE_H
#define QUEUE_H

#include "semaphore.h"
#include "spinlock.h"
#include "status.h"

typedef struct Queue Queue;

struct Queue {
  uint8_t *buf;
  uint32_t head;
  uint32_t tail;
  uint32_t capacity;   // max number of items in the queue
  uint32_t item_size;  // bytes per item

  Semaphore space_available;
  Semaphore data_available;

  Spinlock lock;
#ifdef RTOS_TELEMETRY
  // See Mutex::sync_id (mutex.h). space_available/data_available get their own
  // sync_ids (via semaphore_init_with_parent()), each linked back to this one
  // as parent_sync_id — see md/client/device/sync_view.md.
  uint16_t sync_id;
#endif
};

/**
 * @brief Initialize a fixed-capacity message queue, allocating its backing buffer from the kernel heap.
 * @param name Only used (broadcast once, never stored) when built with RTOS_TELEMETRY; pass NULL or a literal freely either way.
 * @return E_OUT_OF_MEM if the heap allocation fails.
 */
StatusCode queue_init(Queue *q, uint32_t capacity, uint32_t item_size, const char *name);

/**
 * @brief Copy one item into the queue, blocking up to timeout_ms if it's full.
 */
StatusCode queue_send(Queue *q, const void *src, int64_t timeout_ms);

/**
 * @brief Copy one item out of the queue, blocking up to timeout_ms if it's empty.
 */
StatusCode queue_recv(Queue *q, void *dst, int64_t timeout_ms);

#endif
