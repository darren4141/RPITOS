#ifndef JTAG_H
#define JTAG_H

/**
 * @brief Configure the JTAG pins (ARM_TMS/TDI/TCK/TDO/TRST) as ALT4 for OpenOCD debug access.
 */
void jtag_gpio_init(void);

#endif
