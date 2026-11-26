#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# usb-stream-host.py -- the PC side of usb-cdc-stream (CH32V307V-EVT, USB HS
# port P6): send a file to the board's CDC-ACM device and save what it
# streams back as <file>.back, then compare.
#
#     usb-stream-host.py [file] [seconds]       (default: name.dat, 120)
#
# The device is found by VID:PID cafe:4014 (sysfs). The tty is put in raw
# mode before anything is sent, so the line discipline neither echoes nor
# translates. The board echoes as it receives, so this side writes and reads
# at the same time (select), never only one of them.
#
# Protocol (little-endian 32-bit; CRC-32 as zlib.crc32):
#   PC -> board   b"UOSF" length  <file>
#   board -> PC   b"UOSE" length  <file, streamed back>  b"UOSC" crc32
#   PC -> board   b"K" (file.back identical and both CRCs agree) or b"X"
#
# Exit 0 PASS, 1 FAIL, 2 no device / timeout.
# -----------------------------------------------------------------------------
import glob
import os
import select
import struct
import sys
import termios
import time
import tty
import zlib

VID, PID = 0xCAFE, 0x4014


def log(msg):
    print(f"usb-stream-host: {msg}", flush=True)


def find_tty(vid, pid):
    for t in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        dev = os.path.realpath(os.path.join(t, "device"))
        # .../<usb device>/<interface>: the IDs are on the device, one up.
        usbdev = os.path.dirname(dev)
        try:
            with open(os.path.join(usbdev, "idVendor")) as f:
                v = int(f.read(), 16)
            with open(os.path.join(usbdev, "idProduct")) as f:
                p = int(f.read(), 16)
        except OSError:
            continue
        if (v, p) == (vid, pid):
            return "/dev/" + os.path.basename(t)
    return None


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    name = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "name.dat")
    secs = int(sys.argv[2]) if len(sys.argv) > 2 else 120

    with open(name, "rb") as f:
        data = f.read()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    log(f"{name}: {len(data)} bytes, CRC-32 {crc:08x}")

    deadline = time.time() + secs
    dev = None
    while dev is None and time.time() < deadline:
        dev = find_tty(VID, PID)
        if dev is None:
            time.sleep(0.2)
    if dev is None:
        log(f"no CDC-ACM device {VID:04x}:{PID:04x} within {secs} s")
        return 2
    log(f"device {dev}")

    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        tty.setraw(fd)
        termios.tcflush(fd, termios.TCIOFLUSH)

        out = b"UOSF" + struct.pack("<I", len(data)) + data
        sent = 0
        rx = bytearray()
        need = 8 + len(data) + 8
        t0 = time.time()
        last = t0
        while len(rx) < need:
            if time.time() - last > 60:
                log(f"no progress for 60 s: sent {sent}, received {len(rx)}")
                return 2
            wlist = [fd] if sent < len(out) else []
            r, w, _ = select.select([fd], wlist, [], 1.0)
            if w:
                n = os.write(fd, out[sent:sent + 4096])
                if n > 0:
                    sent += n
                    last = time.time()
            if r:
                b = os.read(fd, 65536)
                if b:
                    rx += b
                    last = time.time()
        dt = time.time() - t0

        if rx[0:4] != b"UOSE" or struct.unpack("<I", rx[4:8])[0] != len(data):
            log(f"bad echo header {bytes(rx[0:8])!r}")
            os.write(fd, b"X")
            return 1
        back = bytes(rx[8:8 + len(data)])
        tail = bytes(rx[8 + len(data):need])
        if tail[0:4] != b"UOSC":
            log(f"bad trailer {tail!r}")
            os.write(fd, b"X")
            return 1
        bcrc = struct.unpack("<I", tail[4:8])[0]

        with open(name + ".back", "wb") as f:
            f.write(back)
        same = back == data
        crc_ok = bcrc == crc and (zlib.crc32(back) & 0xFFFFFFFF) == crc
        log(f"round trip {len(data)} bytes in {dt:.2f} s "
            f"({len(data) / 1024 / dt:.0f} KiB/s each way); "
            f"{name}.back {'identical' if same else 'DIFFERS'}, "
            f"board CRC-32 {bcrc:08x} {'agrees' if crc_ok else 'DISAGREES'}")
        os.write(fd, b"K" if same and crc_ok else b"X")
        time.sleep(0.2)
        log("PASS" if same and crc_ok else "FAIL")
        return 0 if same and crc_ok else 1
    finally:
        os.close(fd)


if __name__ == "__main__":
    sys.exit(main())
