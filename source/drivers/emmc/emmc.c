#include "delay.h"
#include "emmc.h"
#include "uart.h"

static uint32_t ulRCA = 1;
static int xIsHC = 1;   // CM4 eMMC is always high-capacity (sector addressing)
static int s_emmc_initialized = 0;

#define EMMC2_BASE_CLOCK 200000000U

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void emmc_delay_us(uint32_t us)
{
  volatile uint32_t i = us * 100;
  while (i--) {}
}

static StatusCode emmc_wait_interrupt(uint32_t ulMask, uint32_t ulTimeoutMs)
{
  uint32_t ulCount = ulTimeoutMs * 1000;
  while (!(pxEMMC->INTERRUPT & ulMask)) {
    if (pxEMMC->INTERRUPT & INT_ERROR_MASK) {
      uart_printf("emmc: int error 0x%08X (waiting 0x%08X)\r\n",
                  pxEMMC->INTERRUPT, ulMask);
      pxEMMC->INTERRUPT = 0xFFFFFFFF;
      return E_CMD;
    }
    if (--ulCount == 0) {
      uart_printf("emmc: int timeout mask=0x%08X INT=0x%08X STATUS=0x%08X\r\n",
                  ulMask, pxEMMC->INTERRUPT, pxEMMC->STATUS);
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }
  if (pxEMMC->INTERRUPT & INT_ERROR_MASK) {
    uart_printf("emmc: int error 0x%08X (after mask=0x%08X)\r\n",
                pxEMMC->INTERRUPT, ulMask);
    pxEMMC->INTERRUPT = 0xFFFFFFFF;
    return E_CMD;
  }
  pxEMMC->INTERRUPT = ulMask;
  return E_OK;
}

static StatusCode emmc_wait_status(uint32_t ulMask, uint32_t ulTimeoutMs)
{
  uint32_t ulCount = ulTimeoutMs * 1000;
  while (pxEMMC->STATUS & ulMask) {
    if (--ulCount == 0) {
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }
  return E_OK;
}

static StatusCode emmc_send_command(uint32_t ulCmd, uint32_t ulArg)
{
  if (emmc_wait_status(STATUS_CMD_INHIBIT, 2000) != E_OK) {
    uart_printf("emmc: CMD_INHIBIT stuck STATUS=0x%08X\r\n", pxEMMC->STATUS);
    return E_TIMED_OUT;
  }

  // R1b (busy-after-response) commands also need DAT line free
  if ((ulCmd & (3u << 16)) == CMD_RESP_48B) {
    if (emmc_wait_status(STATUS_DAT_INHIBIT, 2000) != E_OK) {
      uart_printf("emmc: DAT_INHIBIT stuck STATUS=0x%08X\r\n", pxEMMC->STATUS);
      return E_TIMED_OUT;
    }
  }

  pxEMMC->INTERRUPT = 0xFFFFFFFF;
  pxEMMC->ARG1 = ulArg;
  pxEMMC->CMDTM = ulCmd;

  // No-response commands (CMD0): wait for CMD_INHIBIT to clear — no INT_CMD_DONE fires
  if ((ulCmd & (3u << 16)) == CMD_RESP_NONE) {
    emmc_delay_us(200);      // > 48 clocks at 400 KHz (~120 µs)
    pxEMMC->INTERRUPT = 0xFFFFFFFF;
    return emmc_wait_status(STATUS_CMD_INHIBIT, 100);
  }

  StatusCode xRet = emmc_wait_interrupt(INT_CMD_DONE, 2000);
  if (xRet != E_OK) {
    return xRet;
  }

  // R1b: wait for card to deassert busy on DAT0
  if ((ulCmd & (3u << 16)) == CMD_RESP_48B) {
    emmc_delay_us(100);
    if (emmc_wait_status(STATUS_DAT_ACTIVE, 5000) != E_OK) {
      uart_printf("emmc: R1b busy timeout STATUS=0x%08X\r\n", pxEMMC->STATUS);
      return E_TIMED_OUT;
    }
  }

  return E_OK;
}

static StatusCode emmc_set_clock(uint32_t ulHz)
{
  if (emmc_wait_status(STATUS_CMD_INHIBIT | STATUS_DAT_INHIBIT, 2000) != E_OK) {
    return E_TIMED_OUT;
  }

  pxEMMC->CONTROL1 &= ~CTRL1_CLK_EN;
  emmc_delay_us(10);

  uint32_t ulDiv = EMMC2_BASE_CLOCK / (2u * ulHz);
  if (ulDiv < 1) {
    ulDiv = 1;
  }
  if (ulDiv > 0x3FF) {
    ulDiv = 0x3FF;
  }

  uint32_t ulCtrl1 = pxEMMC->CONTROL1;
  ulCtrl1 &= ~0xFFE0u;
  ulCtrl1 |= ((ulDiv & 0xFF) << 8) | (((ulDiv >> 8) & 0x3) << 6) | CTRL1_CLK_INTLEN;
  pxEMMC->CONTROL1 = ulCtrl1;
  emmc_delay_us(20);

  uint32_t ulCount = 10000;
  while (!(pxEMMC->CONTROL1 & CTRL1_CLK_STABLE)) {
    if (--ulCount == 0) {
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }

  pxEMMC->CONTROL1 |= CTRL1_CLK_EN;
  emmc_delay_us(20);
  return E_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

StatusCode emmc_init(void)
{
  if (s_emmc_initialized) {
    return E_OK;
  }

  // If a previous boot stage already initialized the controller, the SD clock
  // will be enabled and stable. Skip re-init to avoid disrupting an active card.
  if ((pxEMMC->CONTROL1 & (CTRL1_CLK_EN | CTRL1_CLK_STABLE)) == (CTRL1_CLK_EN | CTRL1_CLK_STABLE)) {
    s_emmc_initialized = 1;
    return E_OK;
  }

  uart_print("emmc: init\r\n");

  // Reset host + CMD + DATA circuits
  pxEMMC->CONTROL0 = 0;
  pxEMMC->CONTROL1 = 0;
  pxEMMC->CONTROL1 |= CTRL1_SRST_HC | CTRL1_SRST_CMD | CTRL1_SRST_DATA;
  uint32_t ulCount = 10000;
  while (pxEMMC->CONTROL1 & (CTRL1_SRST_HC | CTRL1_SRST_CMD | CTRL1_SRST_DATA)) {
    if (--ulCount == 0) {
      uart_print("emmc: reset timeout\r\n");return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }

  pxEMMC->CONTROL1 = (pxEMMC->CONTROL1 & ~CTRL1_DATA_TOUNIT) | (0xE << 16);
  pxEMMC->IRPT_MASK = 0xFFFFFFFF;
  pxEMMC->IRPT_EN = 0x00000000;

  if (emmc_set_clock(400000) != E_OK) {
    uart_print("emmc: clock 400KHz failed\r\n");
    return E_TIMED_OUT;
  }

  // SD Bus Power on, 3.3V (CONTROL0[11:9]=111, CONTROL0[8]=1).
  // Without this the EMMC2 controller does not drive the CMD line and
  // CMD_INHIBIT never clears after CMD0.
  pxEMMC->CONTROL0 |= (7u << 9) | (1u << 8);
  emmc_delay_us(10000);       // >1 ms for bus power to stabilise

  // CMD0 — go idle
  emmc_send_command(CMD0, 0); // failure non-fatal; card may already be idle
  emmc_delay_us(2000);

  // CMD1 — send op cond (eMMC-specific; SD uses ACMD41)
  // 0x40FF8080 = HC request | dual-voltage 3.3V/1.8V | sector addressing
  ulCount = 1000;
  uint32_t ulOCR = 0;
  do {
    if (emmc_send_command(CMD1, 0x40FF8080) != E_OK) {
      uart_printf("emmc: CMD1 fail INT=0x%08X\r\n", pxEMMC->INTERRUPT);
      return E_CMD;
    }
    ulOCR = pxEMMC->RESP0;
    if (--ulCount == 0) {
      uart_print("emmc: CMD1 timeout\r\n");return E_TIMED_OUT;
    }
    emmc_delay_us(1000);
  } while (!(ulOCR & (1u << 31)));

  xIsHC = (ulOCR & (1u << 30)) ? 1 : 0;

  // CMD2 — get CID (136-bit response, just ignore the payload)
  if (emmc_send_command(CMD2, 0) != E_OK) {
    return E_CMD;
  }

  // CMD3 — set RCA (eMMC: host assigns RCA via arg; response is R1 card status, NOT an RCA echo)
  ulRCA = 1;
  if (emmc_send_command(CMD3, ulRCA << 16) != E_OK) {
    return E_CMD;
  }

  // CMD7 — select card (move to Transfer state)
  if (emmc_send_command(CMD7, ulRCA << 16) != E_OK) {
    return E_CMD;
  }

  // CMD6 SWITCH — EXT_CSD[183] = 1 (4-bit bus)
  if (emmc_send_command(CMD6_SWITCH, SWITCH_ARG(EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4BIT)) != E_OK) {
    uart_print("emmc: CMD6 failed\r\n");
    return E_CMD;
  }
  pxEMMC->CONTROL0 |= (1u << 1);    // host: 4-bit mode

  if (emmc_set_clock(25000000) != E_OK) {
    uart_print("emmc: 25MHz clock failed\r\n");
    return E_TIMED_OUT;
  }

  s_emmc_initialized = 1;
  return E_OK;
}

StatusCode emmc_read_blocks(uint32_t ulSector, void *pvBuf, uint32_t ulCount)
{
  if (ulCount == 0) {
    return E_OK;
  }

  uint32_t ulArg = xIsHC ? ulSector : ulSector * SECTOR_SIZE;
  uint32_t *pulBuf = (uint32_t *)pvBuf;

  if (ulCount == 1) {
    pxEMMC->BLKSIZECNT = (1u << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD17, ulArg) != E_OK) {
      return E_CMD;
    }
    if (emmc_wait_interrupt(INT_READ_RDY, 2000) != E_OK) {
      return E_TIMED_OUT;
    }
    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
      pulBuf[i] = pxEMMC->DATA;
    }
  }
  else {
    pxEMMC->BLKSIZECNT = (ulCount << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD18, ulArg) != E_OK) {
      return E_CMD;
    }
    for (uint32_t b = 0; b < ulCount; b++) {
      if (emmc_wait_interrupt(INT_READ_RDY, 2000) != E_OK) {
        return E_TIMED_OUT;
      }
      for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        pulBuf[b * (SECTOR_SIZE / 4) + i] = pxEMMC->DATA;
      }
    }
  }

  if (emmc_wait_interrupt(INT_DATA_DONE, 2000) != E_OK) {
    return E_TIMED_OUT;
  }
  return E_OK;
}

StatusCode emmc_write_blocks(uint32_t ulSector, const void *pvBuf, uint32_t ulCount)
{
  if (ulCount == 0) {
    return E_OK;
  }

  uint32_t ulArg = xIsHC ? ulSector : ulSector * SECTOR_SIZE;
  const uint32_t *pulBuf = (const uint32_t *)pvBuf;

  if (ulCount == 1) {
    pxEMMC->BLKSIZECNT = (1u << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD24, ulArg) != E_OK) {
      return E_CMD;
    }
    if (emmc_wait_interrupt(INT_WRITE_RDY, 2000) != E_OK) {
      return E_TIMED_OUT;
    }
    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
      pxEMMC->DATA = pulBuf[i];
    }
  }
  else {
    pxEMMC->BLKSIZECNT = (ulCount << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD25, ulArg) != E_OK) {
      return E_CMD;
    }
    for (uint32_t b = 0; b < ulCount; b++) {
      if (emmc_wait_interrupt(INT_WRITE_RDY, 2000) != E_OK) {
        return E_TIMED_OUT;
      }
      for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        pxEMMC->DATA = pulBuf[b * (SECTOR_SIZE / 4) + i];
      }
    }
  }

  if (emmc_wait_interrupt(INT_DATA_DONE, 5000) != E_OK) {
    return E_TIMED_OUT;
  }
  if (emmc_wait_status(STATUS_DAT_ACTIVE, 5000) != E_OK) {
    return E_TIMED_OUT;
  }
  return E_OK;
}
