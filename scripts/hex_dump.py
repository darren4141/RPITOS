#!/usr/bin/env python3
"""Live hex dump of a serial port — for reading binary protocols (e.g. the
telemetry packet stream) that a plain-text terminal would render as garbage.

Usage:
    python hex_dump.py <port> [baud] [--width N]

Examples:
    python hex_dump.py COM10
    python hex_dump.py COM10 921600
    python hex_dump.py COM10 115200 --width 8

Requires: pip install pyserial
Exit: Ctrl+C
"""

import argparse
import sys

try:
    import serial
except ImportError:
    print("Error: pyserial not installed.  Run: pip install pyserial")
    sys.exit(1)


def to_ascii_gutter(chunk: bytes) -> str:
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Serial port, e.g. COM10")
    parser.add_argument("baud", nargs="?", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("--width", type=int, default=16, help="Bytes per line (default: 16)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Error opening {args.port}: {e}")
        sys.exit(1)

    print(f"--- hex dump active on {args.port} @ {args.baud} (Ctrl+C to exit) ---")

    offset = 0
    line = bytearray()
    try:
        while True:
            chunk = ser.read(args.width - len(line))
            if not chunk:
                continue
            line += chunk
            if len(line) == args.width:
                hex_bytes = " ".join(f"{b:02X}" for b in line)
                print(f"{offset:08X}  {hex_bytes}  |{to_ascii_gutter(line)}|", flush=True)
                offset += len(line)
                line.clear()
    except KeyboardInterrupt:
        if line:
            hex_bytes = " ".join(f"{b:02X}" for b in line)
            padding = "   " * (args.width - len(line))
            print(f"{offset:08X}  {hex_bytes}{padding}  |{to_ascii_gutter(line)}|", flush=True)
        print("\n--- exiting ---")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
