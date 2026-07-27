# jtag

Enables JTAG on the CM4's ARM debug pins for OpenOCD/GDB access. See the
`hardware-debug` workflow for how this is used in practice.

| Signal | GPIO |
|---|---|
| TRST | 22 |
| TDO | 24 |
| TCK | 25 |
| TDI | 26 |
| TMS | 27 |

Requires setting the JTAG enable bit in the ARM Local `CHIPCTL_A` register
(`0xFF800000`) in addition to configuring the pins as `ALT4` — the pin mux
alone isn't enough to route the debug controller to these pins.
