#include "mailbox.h"

#include <stdint.h>

#include "dma.h"   // BUS_ADDRESS() — mailbox messages use the same VC-visible RAM alias as a DMA source
#include "uart.h"

// ─────────────────────────────────────────────────────────────────────────────
// VideoCore mailbox, property-tag channel (channel 8)
// ─────────────────────────────────────────────────────────────────────────────

#define MBOX_BASE         0xFE00B880UL
#define MBOX_READ         (*(volatile uint32_t *)(MBOX_BASE + 0x00))
#define MBOX_STATUS       (*(volatile uint32_t *)(MBOX_BASE + 0x18))
#define MBOX_WRITE        (*(volatile uint32_t *)(MBOX_BASE + 0x20))

#define MBOX_STATUS_FULL  (1U << 31)
#define MBOX_STATUS_EMPTY (1U << 30)

#define MBOX_TAG_RESP_BIT (1U << 31)

#define MBOX_SPIN_LIMIT   1000000U      // arbitrary — not calibrated against real VC response latency, see docs.md

StatusCode mbox_property_call(volatile uint32_t *buf)
{
  uint32_t addr = BUS_ADDRESS(buf) | MBOX_CH_PROPERTY;

  uint32_t spins = MBOX_SPIN_LIMIT;
  while ((MBOX_STATUS & MBOX_STATUS_FULL) && spins) {
    spins--;
  }
  if (spins == 0) {
    return E_TIMED_OUT;
  }

  MBOX_WRITE = addr;

  uint32_t resp;
  spins = MBOX_SPIN_LIMIT;
  do {
    while ((MBOX_STATUS & MBOX_STATUS_EMPTY) && spins) {
      spins--;
    }
    if (spins == 0) {
      return E_TIMED_OUT;
    }
    resp = MBOX_READ;
  } while (resp != addr);   // ignore responses on a channel/buffer we didn't send

  if (buf[1] != MBOX_TAG_RESP_BIT) {
    return E_CORRUPTED;   // VC reported the overall request failed
  }
  return E_OK;
}

// Shared by every convenience wrapper below — the mailbox hardware only ever
// has one transaction in flight at a time, so there's nothing to gain from
// separate buffers per call. NOT safe to call these concurrently from two
// cores/tasks; add a spinlock around mbox_property_call() if that's ever needed.
static uint32_t s_mbox_buf[8] __attribute__((aligned(16)));

StatusCode mbox_get_clock_rate(uint32_t clock_id, uint32_t *out_hz)
{
  s_mbox_buf[0] = 8 * 4;   // total buffer size, bytes
  s_mbox_buf[1] = 0;       // request code
  s_mbox_buf[2] = MBOX_TAG_GET_CLOCK_RATE;
  s_mbox_buf[3] = 8;       // value buffer size: clock id (in) + rate (out)
  s_mbox_buf[4] = 0;       // request/response indicator
  s_mbox_buf[5] = clock_id;
  s_mbox_buf[6] = 0;       // rate, filled in by the VC
  s_mbox_buf[7] = 0;       // end tag

  StatusCode ret = mbox_property_call(s_mbox_buf);
  if (ret != E_OK) {
    return ret;
  }
  if ((s_mbox_buf[4] & MBOX_TAG_RESP_BIT) == 0) {
    return E_CORRUPTED;
  }

  *out_hz = s_mbox_buf[6];
  return E_OK;
}

StatusCode mbox_get_board_serial(uint32_t *out_serial_lo, uint32_t *out_serial_hi)
{
  s_mbox_buf[0] = 8 * 4;
  s_mbox_buf[1] = 0;
  s_mbox_buf[2] = MBOX_TAG_GET_BOARD_SERIAL;
  s_mbox_buf[3] = 8;       // value buffer size: two 32-bit halves
  s_mbox_buf[4] = 0;
  s_mbox_buf[5] = 0;
  s_mbox_buf[6] = 0;
  s_mbox_buf[7] = 0;

  StatusCode ret = mbox_property_call(s_mbox_buf);
  if (ret != E_OK) {
    return ret;
  }
  if ((s_mbox_buf[4] & MBOX_TAG_RESP_BIT) == 0) {
    return E_CORRUPTED;
  }

  *out_serial_lo = s_mbox_buf[5];
  *out_serial_hi = s_mbox_buf[6];
  return E_OK;
}

StatusCode mbox_get_arm_memory(uint32_t *out_base, uint32_t *out_size)
{
  s_mbox_buf[0] = 8 * 4;
  s_mbox_buf[1] = 0;
  s_mbox_buf[2] = MBOX_TAG_GET_ARM_MEMORY;
  s_mbox_buf[3] = 8;       // value buffer size: base + size
  s_mbox_buf[4] = 0;
  s_mbox_buf[5] = 0;
  s_mbox_buf[6] = 0;
  s_mbox_buf[7] = 0;

  StatusCode ret = mbox_property_call(s_mbox_buf);
  if (ret != E_OK) {
    return ret;
  }
  if ((s_mbox_buf[4] & MBOX_TAG_RESP_BIT) == 0) {
    return E_CORRUPTED;
  }

  *out_base = s_mbox_buf[5];
  *out_size = s_mbox_buf[6];
  return E_OK;
}

StatusCode mbox_selftest(void)
{
  s_mbox_buf[0] = 7 * 4;
  s_mbox_buf[1] = 0;
  s_mbox_buf[2] = MBOX_TAG_GET_FIRMWARE_REVISION;
  s_mbox_buf[3] = 4;       // value buffer size: one 32-bit word
  s_mbox_buf[4] = 0;
  s_mbox_buf[5] = 0;       // firmware revision, filled in by the VC
  s_mbox_buf[6] = 0;       // end tag

  StatusCode ret = mbox_property_call(s_mbox_buf);
  uart_printf("mbox: selftest firmware_rev=0x%08X st=%d\r\n", s_mbox_buf[5], ret);
  return ret;
}
