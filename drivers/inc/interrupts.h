#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

static inline uint32_t enter_critical(void)
{
  uint32_t cpsr;
  __asm volatile ("mrs %0, cpsr" : "=r"(cpsr));
  __asm volatile ("cpsid i"      ::: "memory");
  return cpsr;
}

static inline void exit_critical(uint32_t saved_cpsr)
{
  __asm volatile ("msr cpsr_c, %0" :: "r"(saved_cpsr) : "memory");
}

#endif
