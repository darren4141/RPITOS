#ifndef GENTIMER_H
#define GENTIMER_H

#include "status.h"
#include <stddef.h>
#include <stdint.h>

#define HZ 1000

/**
 * @brief Configure the EL1 physical timer (CNTP) to fire at hz and enable its interrupt.
 */
StatusCode gentimer_init(uint32_t *clk_freq, uint32_t hz);

/**
 * @brief Stop the EL1 physical timer.
 */
void gentimer_disable(void);

/**
 * @brief Read the current physical counter value (CNTPCT).
 */
uint64_t read_cntpct(void);

#endif
