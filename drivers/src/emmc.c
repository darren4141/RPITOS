#include "emmc.h"

#include "delay.h"
#include "uart.h"

static uint32_t ulRCA = 0;  // relative card address
static int xIsHC = 0;       // high capacity card flag

#define EMMC2_BASE_CLOCK 100000000

// Helpers

static void delayMicros(uint32_t us)
{
  volatile uint32_t i = us * 100;
  while (i--) {}
}

static StatusCode blockingWaitInterrupt(uint32_t ulMask, uint32_t ulTimeoutMs)
{
  uint32_t ulCount = ulTimeoutMs * 1000;
  while (!(pxEMMC->INTERRUPT & ulMask)) {
    if (--ulCount == 0) {
      return E_TIMED_OUT;
    }
    delayMicros(1);
  }

  if (pxEMMC->INTERRUPT & INT_ERROR_MASK) {
    uint32_t ulErr = pxEMMC->INTERRUPT & INT_ERROR_MASK;
    pxEMMC->INTERRUPT = ulErr;
    return E_CMD;
  }

  pxEMMC->INTERRUPT = ulMask;
  return E_OK;
}

static StatusCode blockingWaitStatus( uint32_t ulMask, uint32_t ulTimeoutMs )
{
  uint32_t ulCount = ulTimeoutMs * 1000;
  while (pxEMMC->STATUS & ulMask) {
    if (--ulCount == 0) {
      return E_TIMED_OUT;
    }
    delayMicros( 1 );
  }
  return E_OK;
}

static StatusCode sdSetClock(uint32_t ulHz)
{
  // wait for lines to be free
  if (blockingWaitStatus(STATUS_CMD_INHIBIT | STATUS_DAT_INHIBIT, 2000) != E_OK) {
    return E_TIMED_OUT;
  }

  // disable SD clock
  pxEMMC->CONTROL1 &= ~CTRL1_CLK_EN;
  delayMicros(10);

  // BCM2711 EMMC2 base clock is 100MHz
  // divisor = base_clock / (2 * target_freq), rounded up
  uint32_t ulDiv = (EMMC2_BASE_CLOCK / (2 * ulHz));
  if (ulDiv < 1) {
    ulDiv = 1;
  }
  if (ulDiv > 0x3FF) {
    ulDiv = 0x3FF;
  }

  uint32_t ulFreqSel = ulDiv & 0xFF;
  uint32_t ulUpperBits = (ulDiv >> 8) & 0x3;

  uint32_t ulCtrl1 = pxEMMC->CONTROL1;
  ulCtrl1 &= ~(0xFFE0);       // clear existing clock fields
  ulCtrl1 |= (ulFreqSel << 8) | (ulUpperBits << 6) | CTRL1_CLK_INTLEN;
  pxEMMC->CONTROL1 = ulCtrl1;
  delayMicros(20);

  // wait for clock to stabilise
  uint32_t ulCount = 2000;
  while (!(pxEMMC->CONTROL1 & CTRL1_CLK_STABLE)) {
    if (--ulCount == 0) {
      return E_TIMED_OUT;
    }
    delayMicros(1);
  }

  // enable SD clock
  pxEMMC->CONTROL1 |= CTRL1_CLK_EN;
  delayMicros(20);
  return E_OK;
}

static StatusCode sdSendCommand( uint32_t ulCmd, uint32_t ulArg )
{
  // clear interrupt flags
  pxEMMC->INTERRUPT = 0xFFFFFFFF;

  // wait for command line free
  if (blockingWaitStatus( STATUS_CMD_INHIBIT, 2000 ) != E_OK) {
    return E_TIMED_OUT;
  }

  pxEMMC->ARG1 = ulArg;
  pxEMMC->CMDTM = ulCmd;

  // wait for command complete
  int xRet = blockingWaitInterrupt( INT_CMD_DONE, 2000 );
  if (xRet != E_OK) {
    return xRet;
  }

  return E_OK;
}

static StatusCode sdSendACMD( uint32_t ulCmd, uint32_t ulArg )
{
  // ACMD = CMD55 followed by the app command
  int xRet = sdSendCommand( CMD55, ulRCA << 16 );
  if (xRet != E_OK) {
    return xRet;
  }
  return sdSendCommand( ulCmd, ulArg );
}

StatusCode emmc_init(void)
{
  pxEMMC->CONTROL1 |= CTRL1_SRST_HC;
  uint32_t ulCount = 10000;
  while (pxEMMC->CONTROL1 & CTRL1_SRST_HC) {
    if (--ulCount == 0) {
      uart_print( "emmc: reset timeout\r\n" );
      return E_TIMED_OUT;
    }
    delayMicros(1);
  }

  // set data timeout to maximum
  pxEMMC->CONTROL1 = (pxEMMC->CONTROL1 & ~CTRL1_DATA_TOUNIT)
                     | (0xE << 16);

  // enable all interrupts but do not use IRQ — we poll
  pxEMMC->IRPT_MASK = 0xFFFFFFFF;
  pxEMMC->IRPT_EN = 0x00000000;      // no IRQ, polling only

  // set clock to 400KHz for identification
  if (sdSetClock( 400000 ) != E_OK) {
    uart_print( "emmc: clock init failed\r\n" );
    return E_TIMED_OUT;
  }

  // CMD0 — go idle
  sdSendCommand( CMD0, 0 );
  delayMicros( 1000 );

  // CMD8 — send interface condition (3.3V, check pattern 0xAA)
  sdSendCommand( CMD8, 0x1AA );
  if ((pxEMMC->RESP0 & 0xFF) != 0xAA) {
    uart_print( "emmc: CMD8 check pattern mismatch\r\n" );
    // not fatal for eMMC, continue
  }

  // ACMD41 — send op cond, repeat until card ready
  // set HCS bit to indicate we support SDHC/SDXC
  ulCount = 1000;
  uint32_t ulOCR = 0;
  do {
    sdSendACMD( ACMD41, 0x51FF8000 );
    ulOCR = pxEMMC->RESP0;
    if (--ulCount == 0) {
      uart_print( "emmc: ACMD41 timeout\r\n" );
      return E_TIMED_OUT;
    }
    delayMicros( 1000 );
  } while (!(ulOCR & (1 << 31)));

  xIsHC = (ulOCR & (1 << 30)) ? 1 : 0;

  // CMD2 — get CID
  sdSendCommand( CMD2, 0 );

  // CMD3 — get RCA
  sdSendCommand( CMD3, 0 );
  ulRCA = (pxEMMC->RESP0 >> 16) & 0xFFFF;

  // CMD7 — select card
  sdSendCommand( CMD7, ulRCA << 16 );

  // ACMD6 — set 4 bit bus width
  sdSendACMD( ACMD6, 2 );
  pxEMMC->CONTROL0 |= (1 << 1);      // set host to 4 bit mode

  // switch to high speed clock
  if (sdSetClock( 25000000 ) != E_OK) {
    uart_print( "emmc: high speed clock failed\r\n" );
    return E_TIMED_OUT;
  }

  uart_print( "emmc successfully initialized\r\n" );
  return E_OK;
}

StatusCode emmc_read_blocks( uint32_t ulSector, void *pvBuf, uint32_t ulCount )
{
  if (ulCount == 0) {
    return E_OK;
  }

  // byte address for standard capacity, sector address for HC
  uint32_t ulArg = xIsHC ? ulSector : ulSector * SECTOR_SIZE;

  uint32_t *pulBuf = (uint32_t *)pvBuf;

  if (ulCount == 1) {
    // single block read — CMD17
    pxEMMC->BLKSIZECNT = (1 << 16) | SECTOR_SIZE;
    if (sdSendCommand( CMD17, ulArg ) != E_OK) {
      return E_CMD;
    }

    if (blockingWaitInterrupt( INT_READ_RDY, 2000 ) != E_OK) {
      return E_TIMED_OUT;
    }

    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
      pulBuf[i] = pxEMMC->DATA;
    }
  }
  else {
    // multi block read — CMD18
    pxEMMC->BLKSIZECNT = (ulCount << 16) | SECTOR_SIZE;
    if (sdSendCommand( CMD18, ulArg ) != E_OK) {
      return E_CMD;
    }

    for (uint32_t block = 0; block < ulCount; block++) {
      if (blockingWaitInterrupt( INT_READ_RDY, 2000 ) != E_OK) {
        return E_TIMED_OUT;
      }

      for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        pulBuf[ block * (SECTOR_SIZE / 4) + i ] = pxEMMC->DATA;
      }
    }
  }

  if (blockingWaitInterrupt( INT_DATA_DONE, 2000 ) != E_OK) {
    return E_TIMED_OUT;
  }

  return E_OK;
}


StatusCode emmc_write_blocks( uint32_t ulSector, const void *pvBuf, uint32_t ulCount )
{
  if (ulCount == 0) {
    return E_OK;
  }

  uint32_t ulArg = xIsHC ? ulSector : ulSector * SECTOR_SIZE;

  const uint32_t *pulBuf = (const uint32_t *)pvBuf;

  if (ulCount == 1) {
    // single block write — CMD24
    pxEMMC->BLKSIZECNT = (1 << 16) | SECTOR_SIZE;
    if (sdSendCommand( CMD24, ulArg ) != E_OK) {
      return E_CMD;
    }

    if (blockingWaitInterrupt( INT_WRITE_RDY, 2000 ) != E_OK) {
      return E_TIMED_OUT;
    }

    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
      pxEMMC->DATA = pulBuf[i];
    }
  }
  else {
    // multi block write — CMD25
    pxEMMC->BLKSIZECNT = (ulCount << 16) | SECTOR_SIZE;
    if (sdSendCommand( CMD25, ulArg ) != E_OK) {
      return E_CMD;
    }

    for (uint32_t block = 0; block < ulCount; block++) {
      if (blockingWaitInterrupt( INT_WRITE_RDY, 2000 ) != E_OK) {
        return E_TIMED_OUT;
      }

      for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        pxEMMC->DATA = pulBuf[ block * (SECTOR_SIZE / 4) + i ];
      }
    }
  }

  if (blockingWaitInterrupt( INT_DATA_DONE, 5000 ) != E_OK) {
    return E_TIMED_OUT;
  }

  // wait for card to finish programming
  if (blockingWaitStatus( STATUS_DAT_ACTIVE, 5000 ) != E_OK) {
    return E_TIMED_OUT;
  }

  return E_OK;
}