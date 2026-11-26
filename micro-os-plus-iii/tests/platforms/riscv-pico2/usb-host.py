#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# usb-host.py -- the PC's half of the Pico 2 USB device tests, so hw.sh can run
# them unattended.
#
#     usb-host.py cdc [secs]   smp-test-usb-cdc-acm (Cafe:4013, CDC-ACM)
#     usb-host.py hid [secs]   smp-test-usb-hid     (Cafe:4012, HID)
#
# Both tests pass when the host sends Escape (0x1B):
#   cdc  wait for the device's tty (/dev/ttyACM*, found through sysfs by
#        VID:PID), write a line, read the echo back, then send Escape and
#        Return (the test is line-buffered);
#   hid  send an HID OUTPUT report {length, chars...} padded to 64 bytes on
#        the interrupt OUT endpoint -- the format of the test's own
#        host/kbd_forward.c -- first a line, then Escape.
#
# Prints what it did; exit 0 when Escape was sent, 1 if the device never
# appeared within `secs` (default 90), 2 on an I/O error. The Pico 2's own
# micro-USB must be connected to this PC (the Debug Probe is not enough).
# HID needs read/write access to the device node (a udev rule or root).
# -----------------------------------------------------------------------------
import glob
import os
import sys
import termios
import time

VID = 0xCAFE
PIDS = {"cdc": 0x4013, "hid": 0x4012}


def log(msg):
    print(f"[usb-host] {msg}", flush=True)


def find_tty(vid, pid):
    """The tty of a USB device by VID:PID, through sysfs."""
    for tty in glob.glob("/sys/class/tty/ttyACM*"):
        dev = os.path.realpath(os.path.join(tty, "device"))
        # .../usbX/X-Y/X-Y:1.0 -> the device directory is the parent.
        d = os.path.dirname(dev)
        try:
            v = int(open(os.path.join(d, "idVendor")).read(), 16)
            p = int(open(os.path.join(d, "idProduct")).read(), 16)
        except OSError:
            continue
        if v == vid and p == pid:
            return "/dev/" + os.path.basename(tty)
    return None


def run_cdc(secs):
    t0 = time.time()
    tty = None
    while time.time() - t0 < secs:
        tty = find_tty(VID, PIDS["cdc"])
        if tty:
            break
        time.sleep(0.5)
    if not tty:
        log(f"no Cafe:4013 tty within {secs}s")
        return 1
    log(f"device tty {tty}")
    time.sleep(1.0)  # let the device's CDC task settle after enumeration
    try:
        fd = os.open(tty, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(fd)
        attrs[0] = 0  # iflag
        attrs[1] = 0  # oflag
        attrs[3] = 0  # lflag: raw
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        os.write(fd, b"hello from the host\r")
        time.sleep(1.0)
        echo = b""
        try:
            echo = os.read(fd, 4096)
        except BlockingIOError:
            pass
        log(f"echo: {echo!r}")
        # The test reads LINES (completed on Return): Escape counts only
        # inside a completed line.
        os.write(fd, b"\x1b\r")
        time.sleep(0.5)
        os.close(fd)
    except OSError as e:
        log(f"tty error: {e}")
        return 2
    log("sent Escape")
    return 0


def run_hid(secs):
    import usb.core
    import usb.util

    t0 = time.time()
    dev = None
    while time.time() - t0 < secs:
        dev = usb.core.find(idVendor=VID, idProduct=PIDS["hid"])
        if dev is not None:
            break
        time.sleep(0.5)
    if dev is None:
        log(f"no Cafe:4012 within {secs}s")
        return 1
    log("device Cafe:4012 found")
    time.sleep(1.0)
    try:
        intf = dev.get_active_configuration()[(0, 0)]
        if dev.is_kernel_driver_active(intf.bInterfaceNumber):
            dev.detach_kernel_driver(intf.bInterfaceNumber)
        ep = usb.util.find_descriptor(
            intf,
            custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress)
            == usb.util.ENDPOINT_OUT,
        )
        if ep is None:
            log("no interrupt OUT endpoint")
            return 2

        def report(data):
            r = bytes([len(data)]) + data
            return r + bytes(64 - len(r))

        ep.write(report(b"hello from the host\n"), timeout=1000)
        time.sleep(0.5)
        ep.write(report(b"\x1b"), timeout=1000)
    except usb.core.USBError as e:
        log(f"usb error: {e}")
        return 2
    log("sent Escape")
    return 0


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in PIDS:
        print(__doc__ or "usage: usb-host.py cdc|hid [secs]")
        return 2
    secs = int(sys.argv[2]) if len(sys.argv) > 2 else 90
    return run_cdc(secs) if sys.argv[1] == "cdc" else run_hid(secs)


if __name__ == "__main__":
    sys.exit(main())
