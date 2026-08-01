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
};

/**
 * @brief Initialize a fixed-capacity message queue, allocating its backing buffer from the kernel heap.
 * @return E_OUT_OF_MEM if the heap allocation fails.
 */
StatusCode queue_init(Queue *q, uint32_t capacity, uint32_t item_size);

/**
 * @brief Copy one item into the queue, blocking up to timeout_ms if it's full.
 */
StatusCode queue_send(Queue *q, const void *src, int64_t timeout_ms);

/**
 * @brief Copy one item out of the queue, blocking up to timeout_ms if it's empty.
 */
StatusCode queue_recv(Queue *q, void *dst, int64_t timeout_ms);

#endif
