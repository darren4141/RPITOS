#ifndef QUEUE_H
#define QUEUE_H

#include "semaphore.h"
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
};

StatusCode queue_init(Queue *q, uint32_t capacity, uint32_t item_size);
StatusCode queue_send(Queue *q, const void *src, int64_t timeout_ms);
StatusCode queue_recv(Queue *q, void *dst, int64_t timeout_ms);

#endif