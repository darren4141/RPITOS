# dma

BCM2711 legacy DMA controller (channels 0–14).

Channels 0–6 are full "normal" engines, 7–10 are "DMA Lite" (subset, 16-bit
`TXFR_LEN`, no 2D stride), 11–14 are DMA4 (40-bit, different register
layout — not covered here). This driver targets the legacy 32-bit engines.

## Bus addresses

The DMA controller uses VideoCore *bus* addresses, not ARM physical
addresses. RAM buffers and control blocks must be translated with
`BUS_ADDRESS()`; peripheral registers use their `0x7Exxxxxx` bus alias.

`DMA_BUS_ALIAS` is `0xC0000000` — the L2-coherent alias, standard for the
legacy DMA controller. If the mem→mem self-test (`dma_selftest()`) fails to
move data, try `0x40000000` (L2-disabled/direct) instead — that's the one
knob to turn.
