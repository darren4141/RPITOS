#include "dma.h"

#include <stdint.h>

#include "uart.h"

// ─────────────────────────────────────────────────────────────────────────────
// Legacy DMA controller driver (channels 0–10). See dma.h for the address /
// alias caveats — everything the controller touches is a VideoCore bus address.
// ─────────────────────────────────────────────────────────────────────────────

#define DMA_RESET_SPINS  100000U

void dma_channel_init(uint8_t channel)
{
  // Power the channel on in the global ENABLE register (read-modify-write so we
  // don't disturb channels owned by other drivers / firmware).
  DMA_ENABLE |= (1U << channel);
  __asm__ volatile ("dsb sy" ::: "memory");

  volatile DmaChannelRegs *ch = DMA_CHANNEL(channel);

  // Full reset, then wait for the RESET bit to self-clear.
  ch->CS = DMA_CS_RESET;
  uint32_t spins = DMA_RESET_SPINS;
  while ((ch->CS & DMA_CS_RESET) && spins) {
    spins--;
  }

  // Clear any latched status so the first real transfer starts clean.
  ch->CS = DMA_CS_END | DMA_CS_INT;
  __asm__ volatile ("dsb sy" ::: "memory");
}

void dma_start(uint8_t channel, const DmaControlBlock *cb)
{
  volatile DmaChannelRegs *ch = DMA_CHANNEL(channel);

  // Clear stale completion/interrupt latches from a previous transfer.
  ch->CS = DMA_CS_END | DMA_CS_INT;

  // Ensure the control block and its source buffer are in RAM before the engine
  // reads them (caches are off, but this orders the writes ahead of the MMIO kick).
  __asm__ volatile ("dsb sy" ::: "memory");

  ch->CONBLK_AD = BUS_ADDRESS(cb);
  __asm__ volatile ("dsb sy" ::: "memory");

  ch->CS = DMA_CS_ACTIVE;
}

StatusCode dma_wait(uint8_t channel, uint32_t spin_limit)
{
  volatile DmaChannelRegs *ch = DMA_CHANNEL(channel);

  while ((ch->CS & DMA_CS_ACTIVE) && spin_limit) {
    spin_limit--;
  }

  if (ch->CS & DMA_CS_ACTIVE) {
    return E_TIMED_OUT;
  }
  if ((ch->CS & DMA_CS_ERROR) || (ch->DEBUG & DMA_DEBUG_ERROR_MASK)) {
    return E_CORRUPTED;
  }
  return E_OK;
}

// ── Bring-up self-test ────────────────────────────────────────────────────────

#define DMA_SELFTEST_LEN 256U

static uint8_t s_dma_src[DMA_SELFTEST_LEN] __attribute__((aligned(32)));
static uint8_t s_dma_dst[DMA_SELFTEST_LEN] __attribute__((aligned(32)));
static DmaControlBlock s_dma_cb __attribute__((aligned(32)));

StatusCode dma_selftest(uint8_t channel)
{
  for (uint32_t i = 0; i < DMA_SELFTEST_LEN; i++) {
    s_dma_src[i] = (uint8_t)(i ^ 0xA5);
    s_dma_dst[i] = 0;
  }

  s_dma_cb.ti        = DMA_TI_SRC_INC | DMA_TI_DEST_INC | DMA_TI_WAIT_RESP;
  s_dma_cb.source_ad = BUS_ADDRESS(s_dma_src);
  s_dma_cb.dest_ad   = BUS_ADDRESS(s_dma_dst);
  s_dma_cb.txfr_len  = DMA_SELFTEST_LEN;
  s_dma_cb.stride    = 0;
  s_dma_cb.nextconbk = 0;

  dma_channel_init(channel);
  dma_start(channel, &s_dma_cb);

  StatusCode st = dma_wait(channel, 1000000U);
  volatile DmaChannelRegs *ch = DMA_CHANNEL(channel);
  uart_printf("dma: selftest ch%u alias=0x%08X CS=0x%08X DEBUG=0x%08X st=%d\r\n",
              channel, DMA_BUS_ALIAS, ch->CS, ch->DEBUG, st);

  if (st != E_OK) {
    return st;
  }

  // Ordering: make sure we read the DMA-written destination, not a prior value.
  __asm__ volatile ("dsb sy" ::: "memory");
  for (uint32_t i = 0; i < DMA_SELFTEST_LEN; i++) {
    if (s_dma_dst[i] != s_dma_src[i]) {
      uart_printf("dma: selftest MISMATCH at %u (got 0x%02X want 0x%02X)\r\n",
                  i, s_dma_dst[i], s_dma_src[i]);
      return E_CORRUPTED;
    }
  }

  uart_print("dma: selftest OK — mem->mem verified, bus alias correct\r\n");
  return E_OK;
}
