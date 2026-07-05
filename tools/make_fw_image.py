#!/usr/bin/env python3
"""
make_fw_image.py

Prepends a StartPacket header sector to a raw firmware binary, producing an
image ready to write to the correct eMMC raw sector with dd.

Header format (matches dfu_receive.h StartPacket, __attribute__((packed))):
  uint16_t  version_num   — firmware version
  uint32_t  fw_length     — binary size in bytes
  uint32_t  crc           — CRC-32/ISO-HDLC of the binary

The header is placed at offset 0 of the output and zero-padded to one full
512-byte sector.  The binary follows at offset 512.

Usage examples:

  # Bootloader image — write to eMMC sector 1064960 (partition 2 start)
  python make_fw_image.py bootloader.img --target bootloader -o bootloader_with_header.bin
  dd if=bootloader_with_header.bin of=/dev/sdX bs=512 seek=1064960

  # App image (slot A) — write to eMMC sector 1067008
  python make_fw_image.py full_demo.img --target app_a -o app_with_header.bin
  dd if=app_with_header.bin of=/dev/sdX bs=512 seek=1067008
"""

import argparse
import struct
import zlib
import sys
from pathlib import Path

SECTOR_SIZE = 512

# Matches StartPacket { uint32_t version_num; uint32_t fw_length; uint32_t crc; }
# All fields uint32_t — naturally aligned, no packing required.
_HEADER_FMT  = "<III"
_HEADER_SIZE = struct.calcsize(_HEADER_FMT)   # 12 bytes

# eMMC sector numbers — must match emmc.h. Firmware lives in partition 2, above
# the 512 MB FAT boot partition, so raw dd writes can't corrupt the GPU's files.
_EMMC_FW_BASE = 1064960   # partition-2 start LBA (see `fdisk -lu`); == EMMC_FW_BASE
_EMMC_SECTOR = {
    "bootloader": _EMMC_FW_BASE + 0,       # 1064960
    "app":        _EMMC_FW_BASE + 2048,    # 1067008  (slot A, default)
    "app_a":      _EMMC_FW_BASE + 2048,    # 1067008
    "app_b":      _EMMC_FW_BASE + 18432,   # 1083392
}


def crc32(data: bytes) -> int:
    """
    CRC-32/ISO-HDLC: same polynomial and init/final-XOR as the firmware's
    crc32_start / crc32_update (ARM crc32w/h/b instructions) / crc32_finish.
    """
    return zlib.crc32(data) & 0xFFFFFFFF


def build_image(binary: bytes, version: int) -> bytes:
    fw_length = len(binary)
    checksum  = crc32(binary)

    header_bytes = struct.pack(_HEADER_FMT, version, fw_length, checksum)
    header_sector = header_bytes + b"\x00" * (SECTOR_SIZE - _HEADER_SIZE)

    return header_sector + binary, fw_length, checksum


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepend a StartPacket header sector to a firmware binary."
    )
    parser.add_argument("input",
                        help="Raw firmware binary produced by the build (e.g. bootloader.img)")
    parser.add_argument("--target", choices=["bootloader", "app", "app_a", "app_b"],
                        default="bootloader",
                        help="Target slot — controls the dd seek offset shown in output "
                             "(default: bootloader). 'app' == 'app_a'.")
    parser.add_argument("-o", "--output", default=None,
                        help="Output file (default: <input-stem>_with_header.bin)")
    parser.add_argument("-v", "--version", type=int, default=1,
                        help="Firmware version number written into the header (default: 1)")
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        sys.exit(f"error: input file not found: {args.input}")

    binary = input_path.read_bytes()
    if len(binary) == 0:
        sys.exit("error: input binary is empty")

    output_path = Path(args.output) if args.output else \
                  input_path.with_name(input_path.stem + "_with_header.bin")

    image, fw_length, checksum = build_image(binary, args.version)
    output_path.write_bytes(image)

    sector       = _EMMC_SECTOR[args.target]
    total_sectors = 1 + (fw_length + SECTOR_SIZE - 1) // SECTOR_SIZE

    print(f"Input:        {input_path}  ({fw_length} bytes)")
    print(f"Target:       {args.target}  (eMMC sector {sector})")
    print(f"Version:      {args.version}")
    print(f"CRC32:        0x{checksum:08X}")
    print(f"Sectors:      {total_sectors}  (1 header + {total_sectors - 1} binary)")
    print(f"Output:       {output_path}  ({len(image)} bytes)")
    print()
    print("Write to eMMC (replace /dev/sdX with your device):")
    print(f"  sudo dd if={output_path} of=/dev/sdX bs=512 seek={sector} conv=fsync")


if __name__ == "__main__":
    main()
