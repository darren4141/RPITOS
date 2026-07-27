#ifndef DELAY_H
#define DELAY_H

#include "status.h"
#include <stdint.h>

/**
 * @brief Link the delay library to the scheduler's shared tick counter.
 */
StatusCode delay_init(volatile uint64_t *p_tick_count);

/**
 * @brief Busy-wait for the given number of CPU cycles.
 */
void delay_cycles(uint64_t cycles);

/**
 * @brief Busy-wait for the given number of scheduler ticks (not wall-clock ms).
 */
void delay_ms(uint64_t ticks);

#endif
