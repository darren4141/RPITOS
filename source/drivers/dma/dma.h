#ifndef DMA_H
#define DMA_H

#include <stdint.h>

#include "status.h"

// ─────────────────────────────────────────────────────────────────────────────
// BCM2711 legacy DMA controller (channels 0–14).
//
// Channels 0–6 are full "normal" engines, 7–10 are "DMA Lite" (subset, 16-bit
// TXFR_LEN, no 2D stride), 11–14 are DMA4 (40-bit, different register layout —
// NOT covered here). This driver targets the legacy 32-bit engines.
//
// IMPORTANT — the DMA controller uses VideoCore *bus* addresses, not ARM
// physical addresses. RAM buffers and control blocks must be translated with
// BUS_ADDRESS(); peripheral registers use their 0x7Exxxxxx bus alias.
// ─────────────────────────────────────────────────────────────────────────────

#define DMA_BASE 0xFE007000UL

// Per-channel register block (each channel is 0x100 apart).
typedef struct {
  volatile uint32_t CS;          // 0x00 — control / status
  volatile uint32_t CONBLK_AD;   // 0x04 — control block address (bus addr, 32-byte aligned)
  volatile uint32_t TI;          // 0x08 — transfer information (read-only mirror of CB)
  volatile uint32_t SOURCE_AD;   // 0x0C — source address        (read-only mirror)
  volatile uint32_t DEST_AD;     // 0x10 — destination address   (read-only mirror)
  volatile uint32_t TXFR_LEN;    // 0x14 — transfer length       (read-only mirror)
  volatile uint32_t STRIDE;      // 0x18 — 2D stride             (read-only mirror)
  volatile uint32_t NEXTCONBK;   // 0x1C — next control block    (read-only mirror)
  volatile uint32_t DEBUG;       // 0x20 — debug / error status
} DmaChannelRegs_t;

#define DMA_CHANNEL(n)   ((volatile DmaChannelRegs_t *)(DMA_BASE + (uint32_t)(n) * 0x100))
#define DMA_INT_STATUS   (*(volatile uint32_t *)(DMA_BASE + 0xFE0)) // per-channel pending IRQ bits
#define DMA_ENABLE       (*(volatile uint32_t *)(DMA_BASE + 0xFF0)) // per-channel power/enable bits

// CS — control / status
#define DMA_CS_ACTIVE          (1U << 0)  // set to start; self-clears at END
#define DMA_CS_END             (1U << 1)  // transfer complete (write 1 to clear)
#define DMA_CS_INT             (1U << 2)  // interrupt pending  (write 1 to clear)
#define DMA_CS_DREQ            (1U << 3)  // DREQ line currently asserted
#define DMA_CS_PAUSED          (1U << 4)
#define DMA_CS_DREQ_STOPS_DMA  (1U << 5)
#define DMA_CS_ERROR           (1U << 8)  // error — see DEBUG register
#define DMA_CS_ABORT           (1U << 30) // abort current CB
#define DMA_CS_RESET           (1U << 31) // reset the channel

// DEBUG — error flags
#define DMA_DEBUG_READ_LAST_NOT_SET (1U << 0)
#define DMA_DEBUG_FIFO_ERROR        (1U << 1)
#define DMA_DEBUG_READ_ERROR        (1U << 2)
#define DMA_DEBUG_ERROR_MASK        (DMA_DEBUG_FIFO_ERROR | DMA_DEBUG_READ_ERROR)

// TI — transfer information (written into the control block, not the register)
#define DMA_TI_INTEN           (1U << 0)  // raise interrupt at end of this CB
#define DMA_TI_TDMODE          (1U << 1)  // 2D mode
#define DMA_TI_WAIT_RESP       (1U << 3)  // wait for AXI write response (peripheral writes)
#define DMA_TI_DEST_INC        (1U << 4)  // increment destination address
#define DMA_TI_DEST_WIDTH      (1U << 5)  // 128-bit destination writes
#define DMA_TI_DEST_DREQ       (1U << 6)  // gate destination writes on DREQ
#define DMA_TI_SRC_INC         (1U << 8)  // increment source address
#define DMA_TI_SRC_WIDTH       (1U << 9)  // 128-bit source reads
#define DMA_TI_SRC_DREQ        (1U << 10) // gate source reads on DREQ
#define DMA_TI_PERMAP(dreq)    (((uint32_t)(dreq) & 0x1F) << 16) // peripheral DREQ pacing

// Peripheral DREQ IDs (BCM2711)
#define DMA_DREQ_UART_TX       12
#define DMA_DREQ_UART_RX       14

// GIC interrupt ID for a legacy DMA channel's completion IRQ.
// BCM2711: channel N (0–7) → GIC SPI (80+N) → GIC INTID (32+80+N) = 112+N.
#define DMA_IRQ_INTID(ch)      (112U + (uint32_t)(ch))

// VideoCore bus alias for RAM. 0xC0000000 = L2-coherent alias (standard for the
// legacy DMA controller). If the mem→mem self-test fails to move data, try the
// 0x40000000 (L2-disabled/direct) alias instead — this is the one knob to turn.
#define DMA_BUS_ALIAS          0xC0000000U
#define BUS_ADDRESS(a)         (((uint32_t)(uintptr_t)(a) & ~0xC0000000U) | DMA_BUS_ALIAS)

// Control block — 8 words, must be 32-byte aligned and reside in DMA-visible RAM.
typedef struct {
  uint32_t ti;         // transfer information (DMA_TI_*)
  uint32_t source_ad;  // source bus address
  uint32_t dest_ad;    // destination bus address
  uint32_t txfr_len;   // transfer length in bytes
  uint32_t stride;     // 2D stride (0 in linear mode)
  uint32_t nextconbk;  // bus address of next CB, or 0 to stop
  uint32_t reserved[2];
} __attribute__((aligned(32))) DmaControlBlock_t;

// Power on and reset a channel. Call once before first use.
void dma_channel_init(uint8_t channel);

// Kick off the transfer described by cb on the given channel. Clears any stale
// END/INT status first. Issues a dsb so prior buffer/CB writes are visible.
void dma_start(uint8_t channel, const DmaControlBlock_t *cb);

// Spin until the channel's transfer completes (ACTIVE clears) or times out.
// Returns E_OK, E_TIMED_OUT, or E_CORRUPTED (hardware error flagged).
StatusCode dma_wait(uint8_t channel, uint32_t spin_limit);

// Bring-up self-test: a memory→memory copy that proves the controller works and
// the BUS_ADDRESS() alias is correct. Returns E_OK if the copy verified.
StatusCode dma_selftest(uint8_t channel);

#endif
