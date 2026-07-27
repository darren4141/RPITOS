# gpio

BCM2711 GPIO controller driver (`GPFSEL`/`GPSET`/`GPCLR`/`GPLEV`/`GPPUPPDN`).
Pin numbering is the BCM GPIO number (e.g. 14/15 for UART TX/RX, 16 for the
status LED), not a physical header pin number.

Pull configuration uses `GPPUPPDN` (the BCM2711 2-bit-per-pin scheme) rather
than the legacy `GPPUD`/`GPPUDCLK` sequence used on earlier BCM283x chips —
that legacy register still exists in the register map (as reserved padding)
but this driver never touches it.
