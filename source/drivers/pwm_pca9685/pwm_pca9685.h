#ifndef PWM_PCA9685_H
#define PWM_PCA9685_H

#include <stdint.h>

#include "status.h"

// NXP/TI PCA9685 16-channel, 12-bit PWM I2C LED/servo controller, built on
// top of this project's i2c driver. Register map and prescale formula are
// public PCA9685 datasheet facts, not project-specific hardware — see
// docs.md for the rest.

// ── Register map ──────────────────────────────────────────────────────────────
#define PCA9685_MODE1            0x00U
#define PCA9685_MODE2            0x01U
#define PCA9685_PRE_SCALE        0xFEU
#define PCA9685_LED0_ON_L        0x06U        // LEDn_ON_L = LED0_ON_L + 4*n; ON_H/OFF_L/OFF_H follow at +1/+2/+3

// MODE1 bits
#define PCA9685_MODE1_RESTART    (1 << 7)
#define PCA9685_MODE1_EXTCLK     (1 << 6)
#define PCA9685_MODE1_AI         (1 << 5)        // register auto-increment
#define PCA9685_MODE1_SLEEP      (1 << 4)
#define PCA9685_MODE1_ALLCALL    (1 << 0)

// MODE2 bits
#define PCA9685_MODE2_OUTDRV     (1 << 2)        // totem-pole output driver (vs open-drain)

// LEDn_ON_H / LEDn_OFF_H — bit 4 is a full-on/full-off override, independent
// of the 12-bit tick value (low byte + low nibble of the high byte).
#define PCA9685_LED_FULL_BIT     (1 << 4)

#define PCA9685_I2C_ADDR_DEFAULT 0x47U

#define PCA9685_NUM_CHANNELS     16U
#define PCA9685_TICK_MAX         4096U        // 12-bit counter, valid ticks are 0-4095

// Internal oscillator frequency, Hz — used to compute PRE_SCALE for a target
// update rate. Only valid with MODE1.EXTCLK clear (internal osc — the only
// mode this driver supports).
#define PCA9685_OSC_CLOCK_HZ     25000000U

// Valid update-rate range for PRE_SCALE, per datasheet.
#define PCA9685_FREQ_HZ_MIN      24U
#define PCA9685_FREQ_HZ_MAX      1526U

/**
 * @brief Per-instance PCA9685 configuration.
 * @note Pointer-owned by the caller, static/global storage duration — same
 * rule as UartConfig/I2cConfig. channel is the literal BCM2711 I2C number
 * (see i2c/docs.md) the chip is wired to; it must already be initialized via
 * i2c_channel_init() before pwm_pca9685_init() is called.
 */
typedef struct {
  uint8_t channel;
  uint8_t i2c_addr;
  uint32_t pwm_freq_hz;   // target update rate; PCA9685_FREQ_HZ_MIN..PCA9685_FREQ_HZ_MAX
} Pca9685Config;

/**
 * @brief Wake the PCA9685 from reset/power-on sleep and program it for pwm_freq_hz.
 * @note Blocks the calling task (task_delay_ms()) for the datasheet's
 * post-SLEEP-clear settle time — must run in scheduled task context.
 */
StatusCode pwm_pca9685_init(Pca9685Config *config);

/**
 * @brief Force every channel full-off, put the chip back to sleep, and clear the stored config.
 */
StatusCode pwm_pca9685_deinit(void);

/**
 * @brief Query init state.
 * @return E_OK if initialized, E_NOT_INITIALIZED otherwise.
 */
StatusCode pwm_pca9685_is_initialized(void);

/**
 * @brief Set one channel's pulse phase (delay) and width (duty_cycle), each a fraction of UINT32_MAX.
 * @note delay=0/duty_cycle=UINT32_MAX means "high the whole period starting
 * at tick 0". Replaces the original float-based (0.0-1.0) API with uint32_t
 * scaled over its full range — no float math anywhere in this driver, since
 * no FPU context is saved across task switches in this kernel (see CLAUDE.md).
 * Scaled internally to the PCA9685's 12-bit tick resolution.
 * @return E_INVALID_ARGS if delay + duty_cycle overflows UINT32_MAX (the
 * scaled equivalent of the original's delay_percentage + duty_cycle > 1.0 check).
 */
StatusCode pwm_pca9685_set_channel(uint8_t pwm_channel, uint32_t delay, uint32_t duty_cycle);

/**
 * @brief Force one channel fully off (output held low), overriding any ON/OFF ticks.
 */
StatusCode pwm_pca9685_set_channel_full_off(uint8_t pwm_channel);

/**
 * @brief Force one channel fully on (output held high), overriding any ON/OFF ticks.
 */
StatusCode pwm_pca9685_set_channel_full_on(uint8_t pwm_channel);

#endif
