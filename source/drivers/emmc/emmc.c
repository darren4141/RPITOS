#include "delay.h"
#include "emmc.h"
#include "uart.h"

static uint32_t rca = 1;
static int is_high_capacity = 1;             // CM4 eMMC is always high-capacity (sector addressing)
static int s_emmc_initialized = 0;
static int s_adma2_supported = 0; // probed from CAPABILITIES0 at init
static uint8_t  s_device_type = 0; // EXT_CSD DEVICE_TYPE (supported speed modes)
static uint32_t s_sec_count = 0;   // EXT_CSD SEC_COUNT (baseline for bus verify)

#define EMMC2_BASE_CLOCK 200000000U

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void emmc_delay_us(uint32_t us)
{
  volatile uint32_t i = us * 100;
  while (i--) {}
}

static StatusCode emmc_wait_interrupt(uint32_t mask, uint32_t timeout_ms)
{
  uint32_t count = timeout_ms * 1000;
  while (!(emmc_regs->INTERRUPT & mask)) {
    if (emmc_regs->INTERRUPT & INT_ERROR_MASK) {
      uart_printf("emmc: int error 0x%08X (waiting 0x%08X)\r\n",
                  emmc_regs->INTERRUPT, mask);
      emmc_regs->INTERRUPT = 0xFFFFFFFF;
      return E_CMD;
    }
    if (--count == 0) {
      uart_printf("emmc: int timeout mask=0x%08X INT=0x%08X STATUS=0x%08X\r\n",
                  mask, emmc_regs->INTERRUPT, emmc_regs->STATUS);
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }
  if (emmc_regs->INTERRUPT & INT_ERROR_MASK) {
    uart_printf("emmc: int error 0x%08X (after mask=0x%08X)\r\n",
                emmc_regs->INTERRUPT, mask);
    emmc_regs->INTERRUPT = 0xFFFFFFFF;
    return E_CMD;
  }
  emmc_regs->INTERRUPT = mask;
  return E_OK;
}

static StatusCode emmc_wait_status(uint32_t mask, uint32_t timeout_ms)
{
  uint32_t count = timeout_ms * 1000;
  while (emmc_regs->STATUS & mask) {
    if (--count == 0) {
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }
  return E_OK;
}

static StatusCode emmc_send_command(uint32_t cmd, uint32_t arg)
{
  if (emmc_wait_status(STATUS_CMD_INHIBIT, 2000) != E_OK) {
    uart_printf("emmc: CMD_INHIBIT stuck STATUS=0x%08X\r\n", emmc_regs->STATUS);
    return E_TIMED_OUT;
  }

  // R1b (busy-after-response) commands also need DAT line free
  if ((cmd & (3u << 16)) == CMD_RESP_48B) {
    if (emmc_wait_status(STATUS_DAT_INHIBIT, 2000) != E_OK) {
      uart_printf("emmc: DAT_INHIBIT stuck STATUS=0x%08X\r\n", emmc_regs->STATUS);
      return E_TIMED_OUT;
    }
  }

  emmc_regs->INTERRUPT = 0xFFFFFFFF;
  emmc_regs->ARG1 = arg;
  emmc_regs->CMDTM = cmd;

  // No-response commands (CMD0): wait for CMD_INHIBIT to clear — no INT_CMD_DONE fires
  if ((cmd & (3u << 16)) == CMD_RESP_NONE) {
    emmc_delay_us(200);      // > 48 clocks at 400 KHz (~120 µs)
    emmc_regs->INTERRUPT = 0xFFFFFFFF;
    return emmc_wait_status(STATUS_CMD_INHIBIT, 100);
  }

  StatusCode ret = emmc_wait_interrupt(INT_CMD_DONE, 2000);
  if (ret != E_OK) {
    return ret;
  }

  // R1b: wait for card to deassert busy on DAT0
  if ((cmd & (3u << 16)) == CMD_RESP_48B) {
    emmc_delay_us(100);
    if (emmc_wait_status(STATUS_DAT_ACTIVE, 5000) != E_OK) {
      uart_printf("emmc: R1b busy timeout STATUS=0x%08X\r\n", emmc_regs->STATUS);
      return E_TIMED_OUT;
    }
  }

  return E_OK;
}

static StatusCode emmc_set_clock(uint32_t hz)
{
  if (emmc_wait_status(STATUS_CMD_INHIBIT | STATUS_DAT_INHIBIT, 2000) != E_OK) {
    return E_TIMED_OUT;
  }

  emmc_regs->CONTROL1 &= ~CTRL1_CLK_EN;
  emmc_delay_us(10);

  uint32_t div = EMMC2_BASE_CLOCK / (2u * hz);
  if (div < 1) {
    div = 1;
  }
  if (div > 0x3FF) {
    div = 0x3FF;
  }

  uint32_t ctrl1 = emmc_regs->CONTROL1;
  ctrl1 &= ~0xFFE0u;
  ctrl1 |= ((div & 0xFF) << 8) | (((div >> 8) & 0x3) << 6) | CTRL1_CLK_INTLEN;
  emmc_regs->CONTROL1 = ctrl1;
  emmc_delay_us(20);

  uint32_t count = 10000;
  while (!(emmc_regs->CONTROL1 & CTRL1_CLK_STABLE)) {
    if (--count == 0) {
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }

  emmc_regs->CONTROL1 |= CTRL1_CLK_EN;
  emmc_delay_us(20);
  return E_OK;
}

// Read the 512-byte EXT_CSD register (eMMC CMD8 SEND_EXT_CSD) into buf. Single
// data block over DAT, same as a CMD17 read but a different command index.
static StatusCode emmc_read_ext_csd(uint8_t *buf)
{
  emmc_regs->BLKSIZECNT = (1u << 16) | SECTOR_SIZE;
  if (emmc_send_command(CMD8, 0) != E_OK) {
    return E_CMD;
  }
  if (emmc_wait_interrupt(INT_READ_RDY, 2000) != E_OK) {
    return E_TIMED_OUT;
  }
  uint32_t *p = (uint32_t *)buf;
  for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
    p[i] = emmc_regs->DATA;
  }
  return emmc_wait_interrupt(INT_DATA_DONE, 2000);
}

// Probe controller capabilities
static void emmc_probe_capabilities(void)
{
  uint32_t cap0 = emmc_regs->CAPABILITIES0;
  s_adma2_supported = (cap0 & CAP0_ADMA2_SUPPORT) ? 1 : 0;
  uart_printf("emmc: CAPABILITIES0=0x%08X ADMA2=%s\r\n",
              cap0, s_adma2_supported ? "yes" : "no");

  static uint8_t ext_csd[SECTOR_SIZE] __attribute__((aligned(4)));
  if (emmc_read_ext_csd(ext_csd) == E_OK) {
    s_device_type = ext_csd[EXT_CSD_DEVICE_TYPE];
    s_sec_count = (uint32_t)ext_csd[EXT_CSD_SEC_COUNT]
                  | ((uint32_t)ext_csd[EXT_CSD_SEC_COUNT + 1] << 8)
                  | ((uint32_t)ext_csd[EXT_CSD_SEC_COUNT + 2] << 16)
                  | ((uint32_t)ext_csd[EXT_CSD_SEC_COUNT + 3] << 24);
    uart_printf("emmc: EXT_CSD rev=%u DEVICE_TYPE=0x%02X bus_width=%u hs_timing=%u\r\n",
                ext_csd[EXT_CSD_REV], s_device_type,
                ext_csd[EXT_CSD_BUS_WIDTH], ext_csd[EXT_CSD_HS_TIMING]);
    uart_printf("emmc: supports%s%s%s%s%s | %u sectors (~%u MB)\r\n",
                (s_device_type & DEVICE_TYPE_HS26) ? " HS26" : "",
                (s_device_type & DEVICE_TYPE_HS52) ? " HS52" : "",
                (s_device_type & DEVICE_TYPE_DDR52_18V) ? " DDR52" : "",
                (s_device_type & DEVICE_TYPE_HS200_18V) ? " HS200" : "",
                (s_device_type & DEVICE_TYPE_HS400_18V) ? " HS400" : "",
                s_sec_count, s_sec_count / 2048u);
  }
  else {
    uart_print("emmc: EXT_CSD read failed\r\n");
  }
}

// Verify the data bus by reading EXT_CSD back and checking SEC_COUNT matches the
// value captured at the 4-bit baseline. A miswired/failed width or too-fast clock
// shows up as a corrupted read-back.
static int emmc_bus_ok(void)
{
  static uint8_t tmp[SECTOR_SIZE] __attribute__((aligned(4)));
  if (emmc_read_ext_csd(tmp) != E_OK) {
    return 0;
  }
  uint32_t sc = (uint32_t)tmp[EXT_CSD_SEC_COUNT]
                | ((uint32_t)tmp[EXT_CSD_SEC_COUNT + 1] << 8)
                | ((uint32_t)tmp[EXT_CSD_SEC_COUNT + 2] << 16)
                | ((uint32_t)tmp[EXT_CSD_SEC_COUNT + 3] << 24);
  return (s_sec_count != 0) && (sc == s_sec_count);
}

// Upgrade from the 4-bit / 25 MHz baseline to High Speed (50 MHz) and 8-bit,
// where the device supports it and the read-back verifies. Every step reverts to
// the last known-good setting on failure, so the boot path can never break.
static void emmc_negotiate_speed(void)
{
  int hs = 0;
  uint32_t width = 4;

  // High Speed: card HS_TIMING=1, host HS-enable, 50 MHz (200 MHz base / 2 / 2).
  if (s_device_type & DEVICE_TYPE_HS52) {
    if (emmc_send_command(CMD6_SWITCH, SWITCH_ARG(EXT_CSD_HS_TIMING, EXT_CSD_HS_TIMING_HS)) == E_OK) {
      emmc_regs->CONTROL0 |= CTRL0_HS_EN;
      if (emmc_set_clock(50000000) == E_OK && emmc_bus_ok()) {
        hs = 1;
      }
      else {
        emmc_set_clock(25000000);
        emmc_regs->CONTROL0 &= ~CTRL0_HS_EN;
        emmc_send_command(CMD6_SWITCH, SWITCH_ARG(EXT_CSD_HS_TIMING, 0));
        uart_print("emmc: HS52 verify failed, staying 25MHz\r\n");
      }
    }
    else {
      uart_print("emmc: HS_TIMING switch NAK\r\n");
    }
  }

  // 8-bit width: switch card + host, verify via EXT_CSD read-back.
  if (emmc_send_command(CMD6_SWITCH, SWITCH_ARG(EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_8BIT)) == E_OK) {
    emmc_regs->CONTROL0 = (emmc_regs->CONTROL0 & ~CTRL0_4BIT) | CTRL0_8BIT;
    if (emmc_bus_ok()) {
      width = 8;
    }
    else {
      emmc_regs->CONTROL0 = (emmc_regs->CONTROL0 & ~CTRL0_8BIT) | CTRL0_4BIT;
      emmc_send_command(CMD6_SWITCH, SWITCH_ARG(EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4BIT));
      uart_print("emmc: 8-bit verify failed, reverted to 4-bit\r\n");
    }
  }

  uart_printf("emmc: mode = %s, %u-bit\r\n",
              hs ? "HS52 (50MHz)" : "legacy (25MHz)", width);
}

// ---------------------------------------------------------------------------
// emmc_init() sub-steps, run in order
// ---------------------------------------------------------------------------

// Returns true if a previous boot stage already brought the controller up
// (SD clock enabled and stable) and re-init should be skipped to avoid
// disrupting an active card.
static int emmc_already_running(void)
{
  return (emmc_regs->CONTROL1 & (CTRL1_CLK_EN | CTRL1_CLK_STABLE)) == (CTRL1_CLK_EN | CTRL1_CLK_STABLE);
}

// Reset the host + CMD + DATA circuits, then configure the data timeout unit
// and interrupt masks. First step of a cold init.
static StatusCode emmc_host_reset(void)
{
  emmc_regs->CONTROL0 = 0;
  emmc_regs->CONTROL1 = 0;
  emmc_regs->CONTROL1 |= CTRL1_SRST_HC | CTRL1_SRST_CMD | CTRL1_SRST_DATA;

  uint32_t count = 10000;
  while (emmc_regs->CONTROL1 & (CTRL1_SRST_HC | CTRL1_SRST_CMD | CTRL1_SRST_DATA)) {
    if (--count == 0) {
      uart_print("emmc: reset timeout\r\n");
      return E_TIMED_OUT;
    }
    emmc_delay_us(1);
  }

  emmc_regs->CONTROL1 = (emmc_regs->CONTROL1 & ~CTRL1_DATA_TOUNIT) | (0xE << 16);
  emmc_regs->IRPT_MASK = 0xFFFFFFFF;
  emmc_regs->IRPT_EN = 0x00000000;

  return E_OK;
}

// Bring the SD clock up to the 400 kHz identification rate and turn on bus
// power. Must run before any command is sent.
static StatusCode emmc_power_on(void)
{
  if (emmc_set_clock(400000) != E_OK) {
    uart_print("emmc: clock 400KHz failed\r\n");
    return E_TIMED_OUT;
  }

  // SD Bus Power on, 3.3V (CONTROL0[11:9]=111, CONTROL0[8]=1).
  // Without this the EMMC2 controller does not drive the CMD line and
  // CMD_INHIBIT never clears after CMD0.
  emmc_regs->CONTROL0 |= (7u << 9) | (1u << 8);
  emmc_delay_us(10000);       // >1 ms for bus power to stabilise

  return E_OK;
}

// Run the eMMC identification sequence: go idle, negotiate operating
// conditions, read the CID, assign an RCA, and select the card into the
// Transfer state. Sets is_high_capacity and rca as a side effect.
static StatusCode emmc_card_identify(void)
{
  // CMD0 — go idle
  emmc_send_command(CMD0, 0); // failure non-fatal; card may already be idle
  emmc_delay_us(2000);

  // CMD1 — send op cond (eMMC-specific; SD uses ACMD41)
  // 0x40FF8080 = HC request | dual-voltage 3.3V/1.8V | sector addressing
  uint32_t count = 1000;
  uint32_t ocr = 0;
  do {
    if (emmc_send_command(CMD1, 0x40FF8080) != E_OK) {
      uart_printf("emmc: CMD1 fail INT=0x%08X\r\n", emmc_regs->INTERRUPT);
      return E_CMD;
    }
    ocr = emmc_regs->RESP0;
    if (--count == 0) {
      uart_print("emmc: CMD1 timeout\r\n");
      return E_TIMED_OUT;
    }
    emmc_delay_us(1000);
  } while (!(ocr & (1u << 31)));

  is_high_capacity = (ocr & (1u << 30)) ? 1 : 0;

  // CMD2 — get CID (136-bit response, just ignore the payload)
  if (emmc_send_command(CMD2, 0) != E_OK) {
    return E_CMD;
  }

  // CMD3 — set RCA (eMMC: host assigns RCA via arg; response is R1 card status, NOT an RCA echo)
  rca = 1;
  if (emmc_send_command(CMD3, rca << 16) != E_OK) {
    return E_CMD;
  }

  // CMD7 — select card (move to Transfer state)
  if (emmc_send_command(CMD7, rca << 16) != E_OK) {
    return E_CMD;
  }

  return E_OK;
}

// Switch the card + host to the 4-bit / 25 MHz baseline. Always works, and
// is what emmc_negotiate_speed() falls back to if it can't upgrade further.
static StatusCode emmc_bus_baseline(void)
{
  if (emmc_send_command(CMD6_SWITCH, SWITCH_ARG(EXT_CSD_BUS_WIDTH, EXT_CSD_BUS_WIDTH_4BIT)) != E_OK) {
    uart_print("emmc: CMD6 failed\r\n");
    return E_CMD;
  }
  emmc_regs->CONTROL0 |= CTRL0_4BIT;    // host: 4-bit mode

  if (emmc_set_clock(25000000) != E_OK) {
    uart_print("emmc: 25MHz clock failed\r\n");
    return E_TIMED_OUT;
  }

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

  if (emmc_already_running()) {
    emmc_probe_capabilities();
    s_emmc_initialized = 1;
    return E_OK;
  }

  uart_print("emmc: init\r\n");

  StatusCode ret = emmc_host_reset();
  if (ret != E_OK) {
    return ret;
  }

  ret = emmc_power_on();
  if (ret != E_OK) {
    return ret;
  }

  ret = emmc_card_identify();
  if (ret != E_OK) {
    return ret;
  }

  ret = emmc_bus_baseline();
  if (ret != E_OK) {
    return ret;
  }

  emmc_probe_capabilities();   // reads EXT_CSD -> s_device_type, s_sec_count
  emmc_negotiate_speed();      // upgrade to HS52 + 8-bit where supported/verified

  s_emmc_initialized = 1;
  return E_OK;
}

// ---------------------------------------------------------------------------
// ADMA2 — multi-block transfers offloaded to the controller (SDHCI Advanced DMA)
// ---------------------------------------------------------------------------

#define ADMA2_MAX_CHUNK       65536U                               // 16-bit len field
#define ADMA2_MAX_XFER_BYTES  (EMMC_SECTOR_SIZE_APP * SECTOR_SIZE) // 8 MB (largest app)
#define ADMA2_MAX_DESCRIPTORS ((ADMA2_MAX_XFER_BYTES / ADMA2_MAX_CHUNK) + 2U)

typedef struct {
  uint32_t attr_len;                                               // (length16 << 16) | attributes; length16 == 0 means 65536
  uint32_t address;                                                // 32-bit buffer address (VA == PA, MMU off)
} Adma2Desc;

static Adma2Desc s_adma2_table[ADMA2_MAX_DESCRIPTORS] __attribute__((aligned(8)));

// Reset the CMD + DATA circuits (not the whole host) — clears a stuck transfer
// so a PIO retry after a failed DMA starts clean. Card selection/clock survive.
static void emmc_reset_cmd_data(void)
{
  emmc_regs->CONTROL1 |= CTRL1_SRST_CMD | CTRL1_SRST_DATA;
  uint32_t c = 10000;
  while ((emmc_regs->CONTROL1 & (CTRL1_SRST_CMD | CTRL1_SRST_DATA)) && --c) {
    emmc_delay_us(1);
  }
}

// Fill the descriptor table for a contiguous buffer. Chunks into <=64 KB Tran
// lines (each a multiple of 512, since transfers are whole sectors) and marks
// the last line END|INT. Returns descriptor count, or 0 if too large.
static uint32_t emmc_build_adma2_table(const void *buf, uint32_t total_bytes)
{
  uint32_t addr = (uint32_t)(uintptr_t)buf;
  uint32_t remaining = total_bytes;
  uint32_t n = 0;

  while (remaining > 0) {
    if (n >= ADMA2_MAX_DESCRIPTORS) {
      return 0;   // too large for the static table -> caller falls back to PIO
    }
    uint32_t chunk = (remaining > ADMA2_MAX_CHUNK) ? ADMA2_MAX_CHUNK : remaining;
    uint32_t len16 = (chunk == ADMA2_MAX_CHUNK) ? 0u : chunk;   // 0 encodes 65536

    s_adma2_table[n].attr_len = (len16 << 16) | ADMA2_DESC_VALID | ADMA2_DESC_ACT_TRAN;
    s_adma2_table[n].address = addr;

    addr += chunk;
    remaining -= chunk;
    n++;
  }

  s_adma2_table[n - 1].attr_len |= ADMA2_DESC_END | ADMA2_DESC_INT;
  return n;
}

// Run a multi-block transfer via ADMA2. cmd is CMD18 (read) or CMD25 (write).
// Returns E_OK, or an error so the caller can retry the transfer in PIO.
static StatusCode emmc_xfer_adma2(uint32_t cmd, uint32_t sector,
                                  const void *buf, uint32_t count, int is_write)
{
  if (emmc_build_adma2_table(buf, count * SECTOR_SIZE) == 0) {
    return E_INVALID_ARGS;
  }

  // Publish the descriptor table before the controller walks it.
  __asm__ volatile ("dsb sy" ::: "memory");

  emmc_regs->CONTROL0 = (emmc_regs->CONTROL0 & ~CTRL0_DMA_SELECT_MASK) | CTRL0_DMA_SELECT_ADMA2;
  emmc_regs->ADMA_ADDRESS = (uint32_t)(uintptr_t)s_adma2_table;

  uint32_t arg = is_high_capacity ? sector : sector * SECTOR_SIZE;
  emmc_regs->BLKSIZECNT = (count << 16) | SECTOR_SIZE;

  if (emmc_send_command(cmd | TM_DMA_EN, arg) != E_OK) {
    return E_CMD;
  }

  // A single wait for the whole transfer — the controller walks every descriptor
  // itself. INT_ERROR_MASK already includes INT_ADMA_ERR, so emmc_wait_interrupt
  // surfaces ADMA faults.
  StatusCode st = emmc_wait_interrupt(INT_DATA_DONE, 2000u + count * 2u);
  if (st != E_OK) {
    uart_printf("emmc: ADMA2 xfer failed st=%d ADMA_ERR=0x%08X\r\n",
                st, emmc_regs->ADMA_ERROR);
    return st;
  }

  if (is_write) {
    if (emmc_wait_status(STATUS_DAT_ACTIVE, 5000) != E_OK) {
      return E_TIMED_OUT;
    }
  }

  __asm__ volatile ("dsb sy" ::: "memory");
  return E_OK;
}

StatusCode emmc_read_blocks(uint32_t sector, void *buf, uint32_t count)
{
  if (count == 0) {
    return E_OK;
  }

  // Multi-block reads go through ADMA2 when supported; fall back to PIO on any
  // failure (table too large, DMA error) after clearing the data circuit.
  if (s_adma2_supported && (count > 1)) {
    if (emmc_xfer_adma2(CMD18, sector, buf, count, 0) == E_OK) {
      return E_OK;
    }
    emmc_reset_cmd_data();
    uart_print("emmc: ADMA2 read -> PIO fallback\r\n");
  }

  uint32_t arg = is_high_capacity ? sector : sector * SECTOR_SIZE;
  uint32_t *words = (uint32_t *)buf;

  if (count == 1) {
    emmc_regs->BLKSIZECNT = (1u << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD17, arg) != E_OK) {
      return E_CMD;
    }
    if (emmc_wait_interrupt(INT_READ_RDY, 2000) != E_OK) {
      return E_TIMED_OUT;
    }
    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
      words[i] = emmc_regs->DATA;
    }
  }
  else {
    emmc_regs->BLKSIZECNT = (count << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD18, arg) != E_OK) {
      return E_CMD;
    }
    for (uint32_t b = 0; b < count; b++) {
      if (emmc_wait_interrupt(INT_READ_RDY, 2000) != E_OK) {
        return E_TIMED_OUT;
      }
      for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        words[b * (SECTOR_SIZE / 4) + i] = emmc_regs->DATA;
      }
    }
  }

  if (emmc_wait_interrupt(INT_DATA_DONE, 2000) != E_OK) {
    return E_TIMED_OUT;
  }
  return E_OK;
}

StatusCode emmc_write_blocks(uint32_t sector, const void *buf, uint32_t count)
{
  if (count == 0) {
    return E_OK;
  }

  // Hard floor: firmware must never write below the firmware region.
  if (sector < EMMC_FIRMWARE_FLOOR) {
    uart_printf("emmc: BLOCKED write to reserved sector %u (floor %u)\r\n",
                sector, (uint32_t)EMMC_FIRMWARE_FLOOR);
    return E_INVALID_ARGS;
  }

  // Multi-block writes go through ADMA2 when supported; fall back to PIO on any
  // failure. (The floor guard above already gated the sector for both paths.)
  if (s_adma2_supported && (count > 1)) {
    if (emmc_xfer_adma2(CMD25, sector, buf, count, 1) == E_OK) {
      return E_OK;
    }
    emmc_reset_cmd_data();
    uart_print("emmc: ADMA2 write -> PIO fallback\r\n");
  }

  uint32_t arg = is_high_capacity ? sector : sector * SECTOR_SIZE;
  const uint32_t *words = (const uint32_t *)buf;

  if (count == 1) {
    emmc_regs->BLKSIZECNT = (1u << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD24, arg) != E_OK) {
      return E_CMD;
    }
    if (emmc_wait_interrupt(INT_WRITE_RDY, 2000) != E_OK) {
      return E_TIMED_OUT;
    }
    for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
      emmc_regs->DATA = words[i];
    }
  }
  else {
    emmc_regs->BLKSIZECNT = (count << 16) | SECTOR_SIZE;
    if (emmc_send_command(CMD25, arg) != E_OK) {
      return E_CMD;
    }
    for (uint32_t b = 0; b < count; b++) {
      if (emmc_wait_interrupt(INT_WRITE_RDY, 2000) != E_OK) {
        return E_TIMED_OUT;
      }
      for (uint32_t i = 0; i < SECTOR_SIZE / 4; i++) {
        emmc_regs->DATA = words[b * (SECTOR_SIZE / 4) + i];
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
