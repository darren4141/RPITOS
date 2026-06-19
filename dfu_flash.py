#!/usr/bin/env python3
"""DFU flash tool + serial terminal for the rpitos bootloader.

Sends a binary image over UART using the rpitos DFU protocol, then stays open
as a serial terminal so you can watch the CM4 boot output.

Usage:
    python dfu_flash.py <port> [image] [--baud BAUD]   # flash then terminal
    python dfu_flash.py <port> --terminal               # terminal only

Examples:
    python dfu_flash.py COM3
    python dfu_flash.py COM3 kernel7l.img --baud 115200
    python dfu_flash.py COM3 --terminal

Requires: pip install pyserial
Exit terminal: Ctrl+C
"""

import argparse
import queue
import struct
import sys
import threading
import time

try:
    import serial
except ImportError:
    print("Error: pyserial not installed.  Run: pip install pyserial")
    sys.exit(1)

# Packet framing
PACKET_SOF = 0xAA
PACKET_EOF = 0xBB

# Commands (must match dfu.h)
CMD_START  = 0x02
CMD_DATA   = 0x03
CMD_FINISH = 0x04
CMD_ABORT  = 0x05

# Responses (must match DFU_ACK / DFU_NACK in dfu.c)
ACK  = 0x06
NACK = 0x15

# Raw remote-DFU trigger key (must match DFU_TRIGGER_KEY in dfu_trigger.h).
# Sent unframed (not part of the CMD_* packet protocol) so a running app can
# recognize it without running the full packet parser.
TRIGGER_KEY = bytes([0xDF, 0x00, 0xDF, 0x00])

# Max payload per DATA packet — LEN % 4 must == 0 (firmware enforces)
MAX_CHUNK = 256  # MAX_DATA_SIZE_BYTES from dfu.h


# ---------------------------------------------------------------------------
# RX router — prints all target output while the DFU protocol runs
# ---------------------------------------------------------------------------

class _RxRouter:
    """
    Background thread that reads every byte from `ser` and routes it:
      - ACK / NACK bytes → internal queue  (when enable()d; protocol code calls read_byte())
      - everything else  → stdout          (printed immediately, always)

    This means UART output from the CM4 is visible even during DFU trigger
    sending and the packet exchange — nothing is silently swallowed.
    """

    def __init__(self, ser: "serial.Serial") -> None:
        self._ser = ser
        self._proto_q: "queue.SimpleQueue[int]" = queue.SimpleQueue()
        self._active = threading.Event()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                n = self._ser.in_waiting
                if n:
                    data = self._ser.read(n)
                    for b in data:
                        if self._active.is_set() and b in (ACK, NACK):
                            self._proto_q.put(b)
                        else:
                            sys.stdout.buffer.write(bytes([b]))
                            sys.stdout.buffer.flush()
                else:
                    time.sleep(0.001)
            except (serial.SerialException, OSError):
                break

    def enable(self) -> None:
        """Route ACK/NACK to the protocol queue instead of stdout."""
        self._active.set()

    def disable(self) -> None:
        """Stop routing ACK/NACK — everything goes to stdout again."""
        self._active.clear()

    def read_byte(self, timeout: float) -> "int | None":
        """Block up to `timeout` seconds for one ACK/NACK byte."""
        try:
            return self._proto_q.get(timeout=timeout)
        except queue.Empty:
            return None

    def stop(self) -> None:
        self._stop.set()


# ---------------------------------------------------------------------------
# Serial terminal
# ---------------------------------------------------------------------------

def run_terminal(ser: "serial.Serial") -> None:
    """
    Bidirectional serial terminal.  Runs until Ctrl+C.

    A background thread prints everything the CM4 sends; the main thread reads
    keystrokes and forwards them to the CM4.
    """
    stop = threading.Event()

    def rx_worker() -> None:
        while not stop.is_set():
            try:
                n = ser.in_waiting
                if n:
                    data = ser.read(n)
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                else:
                    time.sleep(0.001)
            except (serial.SerialException, OSError):
                break

    rx = threading.Thread(target=rx_worker, daemon=True)
    rx.start()

    print("\r\n\033[90m--- serial terminal active (Ctrl+C to exit) ---\033[0m\r\n",
          end="", flush=True)

    try:
        if sys.platform == "win32":
            import msvcrt
            while not stop.is_set():
                if msvcrt.kbhit():
                    ch = msvcrt.getwch()
                    if ch in ("\x00", "\xe0"):
                        msvcrt.getwch()   # discard second byte of extended key
                        continue
                    if ch == "\x03":      # Ctrl+C
                        raise KeyboardInterrupt
                    ser.write(ch.encode("latin-1", errors="replace"))
                else:
                    time.sleep(0.005)
        else:
            import select
            import termios
            import tty
            fd = sys.stdin.fileno()
            old_attrs = termios.tcgetattr(fd)
            try:
                tty.setraw(fd)
                while not stop.is_set():
                    if select.select([sys.stdin], [], [], 0.005)[0]:
                        ch = sys.stdin.read(1)
                        if ch == "\x03":  # Ctrl+C
                            raise KeyboardInterrupt
                        ser.write(ch.encode("latin-1", errors="replace"))
            finally:
                termios.tcsetattr(fd, termios.TCSADRAIN, old_attrs)

    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        print("\r\n\033[90m--- terminal closed ---\033[0m")


# ---------------------------------------------------------------------------
# DFU packet builder
# ---------------------------------------------------------------------------

def build_packet(cmd: int, data: bytes) -> bytes:
    """
    Wire format (from dfu_packet_receive state machine):
        [0xAA] [CMD:1] [LEN_MSB:1] [LEN_LSB:1] [DATA:LEN] [CRC:4 big-endian] [0xBB]

    LEN must be >= 1: the parser always reads DATA[0] from the outer loop byte,
    so a zero-length payload would consume the first CRC byte instead.

    CRC verification is commented out in the firmware so CRC is always 0.
    """
    assert len(data) >= 1, "payload must be at least 1 byte"
    length = len(data)
    return (
        bytes([PACKET_SOF, cmd, (length >> 8) & 0xFF, length & 0xFF])
        + data
        + struct.pack(">I", 0)  # CRC — not verified by firmware
        + bytes([PACKET_EOF])
    )


# ---------------------------------------------------------------------------
# Remote DFU trigger handshake
# ---------------------------------------------------------------------------

def wait_for_dfu_ready(ser: "serial.Serial", router: "_RxRouter",
                        interval_s: float = 0.1,
                        overall_timeout_s: float = 10.0) -> bool:
    """
    Repeatedly sends the raw trigger key until a single ACK byte comes back.
    All non-ACK bytes from the target are printed to stdout by the router.
    """
    print("Waiting for target (sending DFU trigger)...", end="", flush=True)
    deadline = time.time() + overall_timeout_s
    while time.time() < deadline:
        ser.write(TRIGGER_KEY)
        b = router.read_byte(interval_s)
        if b == ACK:
            print(" ready ✓")
            return True
    print(" TIMEOUT")
    return False


# ---------------------------------------------------------------------------
# ACK / NACK handling
# ---------------------------------------------------------------------------

def wait_ack(router: "_RxRouter", label: str, timeout: float = 5.0) -> bool:
    """Block until one ACK/NACK arrives via the router. Returns True on ACK."""
    b = router.read_byte(timeout)
    if b is None:
        print(f"\n  TIMEOUT waiting for ACK after {label}")
        return False
    if b == ACK:
        return True
    if b == NACK:
        print(f"\n  NACK received after {label}")
    else:
        print(f"\n  Unexpected response 0x{b:02x} after {label}")
    return False


def send_abort(ser: "serial.Serial") -> None:
    ser.write(build_packet(CMD_ABORT, bytes([0x00])))
    print("Sent CMD_ABORT.")


# ---------------------------------------------------------------------------
# DFU flash
# ---------------------------------------------------------------------------

def flash(ser: "serial.Serial", image_path: str) -> bool:
    """
    Flash image_path over the already-open serial port.
    Returns True on success, False on failure.
    """
    with open(image_path, "rb") as f:
        img = bytearray(f.read())

    app_length = len(img)

    if app_length == 0:
        print("Error: image file is empty")
        return False
    if app_length > 0xFFFF:
        print(f"Error: image too large ({app_length} bytes, max 65535)")
        return False

    print(f"Image : {image_path}")
    print(f"Size  : {app_length} bytes")
    print()

    # Pad to a multiple of 4 — CMD_DATA requires LEN % 4 == 0
    if app_length % 4:
        img += bytes(4 - app_length % 4)

    router = _RxRouter(ser)
    router.enable()
    try:
        if not wait_for_dfu_ready(ser, router):
            print("Error: target never responded to the DFU trigger key")
            return False

        # ── CMD_START ─────────────────────────────────────────────────────────────
        # StartPacket: version_num (u16 LE), app_length (u16 LE), crc (u32 LE)
        ser.write(build_packet(CMD_START, struct.pack("<HHI", 1, app_length, 0)))
        if not wait_ack(router,"CMD_START"):
            send_abort(ser)
            return False
        print(f"[START ] version=1  length={app_length} B  ✓")

        # ── CMD_DATA ──────────────────────────────────────────────────────────────
        total        = len(img)
        offset       = 0
        chunk_index  = 0
        total_chunks = (total + MAX_CHUNK - 1) // MAX_CHUNK

        while offset < total:
            chunk = bytes(img[offset: offset + MAX_CHUNK])
            if len(chunk) % 4:
                chunk += bytes(4 - len(chunk) % 4)

            ser.write(build_packet(CMD_DATA, chunk))
            chunk_index += 1

            if not wait_ack(router,f"CMD_DATA chunk {chunk_index}/{total_chunks}"):
                send_abort(ser)
                return False

            offset += MAX_CHUNK
            pct    = min(100, 100 * offset // total)
            filled = pct // 5
            bar    = "#" * filled + "-" * (20 - filled)
            print(f"\r[DATA  ] [{bar}] {pct:3d}%  ({chunk_index}/{total_chunks} chunks)",
                  end="", flush=True)

        print()  # newline after progress bar

        # ── CMD_FINISH ────────────────────────────────────────────────────────────
        ser.write(build_packet(CMD_FINISH, bytes([0x00])))
        if not wait_ack(router,"CMD_FINISH"):
            return False
        print("[FINISH] Flash complete  ✓")
        return True

    finally:
        router.disable()
        router.stop()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="DFU flash tool + serial terminal for the rpitos bootloader"
    )
    parser.add_argument("port", help="Serial port (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument(
        "image",
        nargs="?",
        default="kernel7l.img",
        help="Binary image to flash (default: kernel7l.img)",
    )
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("-t", "--terminal", action="store_true",
                        help="Skip flashing and go straight to terminal mode")
    args = parser.parse_args()

    print(f"Port  : {args.port} @ {args.baud} baud")

    with serial.Serial(args.port, args.baud, timeout=5) as ser:
        time.sleep(0.1)  # let UART settle

        if not args.terminal:
            ok = flash(ser, args.image)
            if not ok:
                print("Flash failed — dropping into terminal anyway.")
            print()

        # Always enter terminal after flash (or immediately with --terminal)
        ser.timeout = 0.05  # short timeout for responsive RX polling
        run_terminal(ser)


if __name__ == "__main__":
    main()
