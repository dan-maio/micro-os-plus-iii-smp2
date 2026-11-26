#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# usb-file-host.py -- the PC's half of smp-test-usb-cdc-file (VisionFive 2
# Lite, USB-C CDC-ACM, Cafe:4013): send a file to the board, receive it back,
# save it as <file>.back and compare.
#
#     usb-file-host.py [file] [secs] [size]
#
#   file  default name.dat; created with `size` random bytes (default
#         1048576) when it does not exist
#   secs  how long to wait for the board's tty (default 90)
#
# Protocol (little-endian 32-bit; CRC-32 as zlib.crc32):
#   PC -> board   b"UOSF" length  <file>
#   board -> PC   b"UOSB" length crc32  <file>
#   PC -> board   b"K" when <file>.back == <file> (and the CRCs agree), else b"X"
#
# The tty is put in raw mode (no echo) and flushed before anything is sent;
# the board sends nothing before the PC's header, so nothing is reflected.
# Exit 0 when the copy is identical, 1 when it differs or the board never
# appears, 2 on an I/O error or a timeout.
# -----------------------------------------------------------------------------

import glob
import os
import select
import struct
import sys
import termios
import time
import zlib

VID, PID = 0xCAFE, 0x4013


def log(msg):
    print(f"[usb-file] {msg}", flush=True)


def find_tty(vid, pid):
    for tty in glob.glob("/sys/class/tty/ttyACM*"):
        dev = os.path.realpath(os.path.join(tty, "device"))
        usbdev = os.path.dirname(dev)
        try:
            with open(os.path.join(usbdev, "idVendor")) as f:
                v = int(f.read(), 16)
            with open(os.path.join(usbdev, "idProduct")) as f:
                p = int(f.read(), 16)
        except OSError:
            continue
        if v == vid and p == pid:
            return "/dev/" + os.path.basename(tty)
    return None


def write_all(fd, data, timeout):
    view = memoryview(data)
    end = time.time() + timeout
    while view:
        if time.time() > end:
            raise TimeoutError("write")
        _, w, _ = select.select([], [fd], [], 1.0)
        if w:
            try:
                n = os.write(fd, view[:65536])
                view = view[n:]
            except BlockingIOError:
                pass


def read_exact(fd, n, timeout):
    buf = bytearray()
    end = time.time() + timeout
    while len(buf) < n:
        if time.time() > end:
            raise TimeoutError(f"read: {len(buf)} of {n} bytes")
        r, _, _ = select.select([fd], [], [], 1.0)
        if r:
            try:
                chunk = os.read(fd, min(65536, n - len(buf)))
                buf += chunk
            except BlockingIOError:
                pass
    return bytes(buf)


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "name.dat"
    secs = int(sys.argv[2]) if len(sys.argv) > 2 else 90
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 1024 * 1024

    if not os.path.exists(name):
        with open(name, "wb") as f:
            f.write(os.urandom(size))
        log(f"created {name}: {size} random bytes")
    with open(name, "rb") as f:
        data = f.read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    log(f"{name}: {len(data)} bytes, CRC-32 {crc:08x}")

    t0 = time.time()
    tty = None
    while time.time() - t0 < secs:
        tty = find_tty(VID, PID)
        if tty:
            break
        time.sleep(0.5)
    if not tty:
        log(f"no Cafe:4013 tty within {secs}s")
        return 1
    log(f"device tty {tty}")

    try:
        fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0  # iflag
        attrs[1] = 0  # oflag
        attrs[3] = 0  # lflag: raw, no echo
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        time.sleep(0.5)
        termios.tcflush(fd, termios.TCIOFLUSH)

        t_tx = time.time()
        write_all(fd, b"UOSF" + struct.pack("<I", len(data)) + data, 120)
        t_tx = time.time() - t_tx
        log(f"sent {len(data)} bytes in {t_tx:.2f} s "
            f"({len(data) / 1024 / t_tx:.0f} KiB/s)")

        hdr = read_exact(fd, 12, 120)
        if hdr[:4] != b"UOSB":
            log(f"bad reply header {hdr!r}")
            os.write(fd, b"X")
            os.close(fd)
            return 1
        blen, bcrc = struct.unpack("<II", hdr[4:12])
        t_rx = time.time()
        back = read_exact(fd, blen, 120)
        t_rx = time.time() - t_rx
        log(f"received {blen} bytes back in {t_rx:.2f} s "
            f"({blen / 1024 / t_rx:.0f} KiB/s), board CRC-32 {bcrc:08x}")
        with open(name + ".back", "wb") as f:
            f.write(back)

        same = (back == data)
        crc_ok = (bcrc == crc) and ((zlib.crc32(back) & 0xFFFFFFFF) == crc)
        if not same:
            diff = next(i for i in range(min(len(back), len(data)) + 1)
                        if i >= len(back) or i >= len(data)
                        or back[i] != data[i])
            log(f"{name}.back DIFFERS from {name} (first difference at "
                f"byte {diff}; {len(back)} vs {len(data)} bytes)")
        else:
            log(f"{name}.back is identical to {name}; CRC-32 "
                f"{'agrees' if crc_ok else 'DISAGREES'}")
        os.write(fd, b"K" if same and crc_ok else b"X")
        time.sleep(0.5)
        os.close(fd)
        return 0 if same and crc_ok else 1
    except TimeoutError as e:
        log(f"timeout: {e}")
        return 2
    except OSError as e:
        log(f"tty error: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
