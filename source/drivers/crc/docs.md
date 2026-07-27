# crc

CRC32 (standard zlib/Ethernet polynomial, init 0xFFFFFFFF, final XOR
0xFFFFFFFF) computed with the ARMv8 hardware `crc32b`/`crc32h`/`crc32w`
instructions rather than a software table. `crc32_init()` checks
`ID_ISAR5` to confirm the CPU actually implements the extension before any
of the other functions are used — the Cortex-A72 in the CM4 does, but this
is the one call that would need a software fallback on a CPU without it.
