#include "i2c.h"

#include <stddef.h>

// ── Channel table ─────────────────────────────────────────────────────────────
// SoC-fixed facts, never caller-configurable — see docs.md's "Channel table".

typedef struct {
  BSCRegs *regs;
  uint32_t irq_intid;   // 0 = not wired up / unknown — see docs.md
} I2cHwDescriptor;

static const I2cHwDescriptor i2c_hw_table[I2C_NUM_CHANNELS] = {
  [0] = { .regs = (BSCRegs *)I2C0_BASE, .irq_intid = I2C_IRQ_INTID },
  [1] = { .regs = (BSCRegs *)I2C1_BASE, .irq_intid = I2C_IRQ_INTID },
  [2] = { 0 },                                                        // BSC2 — dedicated to HDMI, unsupported gap
  [3] = { .regs = (BSCRegs *)I2C3_BASE, .irq_intid = I2C_IRQ_INTID },
  [4] = { .regs = (BSCRegs *)I2C4_BASE, .irq_intid = I2C_IRQ_INTID },  // placeholder — pins unverified, see docs.md
  [5] = { .regs = (BSCRegs *)I2C5_BASE, .irq_intid = I2C_IRQ_INTID },  // placeholder — pins unverified, see docs.md
  [6] = { .regs = (BSCRegs *)I2C6_BASE, .irq_intid = I2C_IRQ_INTID },  // placeholder — conflicts with I2C0 on GPIO0, see docs.md
};

// Set by i2c_channel_init(); NULL means that channel isn't configured yet.
static I2cConfig *s_i2c_configs[I2C_NUM_CHANNELS] = { NULL };
