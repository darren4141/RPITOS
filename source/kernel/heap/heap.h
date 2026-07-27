#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>

/**
 * @brief Allocate size bytes from the static kernel heap pool.
 */
void *heap_malloc(uint32_t size);

#endif
