#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# This file is part of the µOS++ project (https://micro-os-plus.github.io/).
# Copyright (c) 2026 Liviu Ionescu. All rights reserved.
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose is hereby granted, under the terms of the MIT license.
#
# If a copy of the license was not distributed with this file, it can be
# obtained from https://opensource.org/licenses/mit.
# -----------------------------------------------------------------------------

# A hardware test whose board is a USB device and needs the PC's side
# (uos_tests_add_hwd / uos_tests_add_openocd's HOST_*; uos-tests-run.cmake):
#
#   uos-hwd-host.py [--vidpid VVVV:PPPP [--settle S]] [--cwd DIR]
#                   --host <host command...> --board <board command...>
#
# runs the board command (the test's session, as without a host side) and,
# beside it, the host command. With --vidpid (a USB device test) the host
# command starts when the test's USB device has enumerated: a device with
# this VID:PID that was not there when the board command started (a new
# bus/device number), so never the device of a previous run or of an image
# left running from flash; then S seconds (default 3) for it to settle.
# Without it (a network test: the host script finds the board on the LAN
# itself, by its beacon) the host command starts with the board command.
#
# The board's output is passed through as it comes (CTest reads the test's
# RESULT line from it); the host's, line by line, prefixed "[host] ". When
# the host command ends by itself with a non-zero status, "RESULT: FAIL (host
# side, exit N)" is printed. A host command still running when the board
# command has ended gets 5 s, then is stopped; that is not a failure (the
# board's verdict stands: its device is gone, and the host side may be
# blocked closing it). The exit status is the board command's, or 1 when the
# host side failed.

import glob
import os
import subprocess
import sys
import threading
import time

lock = threading.Lock()


def emit(text):
    with lock:
        sys.stdout.write(text)
        sys.stdout.flush()


def devices(vid, pid):
    found = set()
    for d in glob.glob("/sys/bus/usb/devices/*"):
        try:
            with open(d + "/idVendor") as f:
                v = f.read().strip()
            with open(d + "/idProduct") as f:
                p = f.read().strip()
            if v == vid and p == pid:
                with open(d + "/busnum") as f:
                    bus = f.read().strip()
                with open(d + "/devnum") as f:
                    dev = f.read().strip()
                found.add((bus, dev))
        except OSError:
            pass
    return found


def main():
    args = sys.argv[1:]
    if "--host" not in args or "--board" not in args:
        sys.exit("uos-hwd-host.py: --host and --board are required")
    opts = args[: args.index("--host")]
    host_cmd = args[args.index("--host") + 1 : args.index("--board")]
    board_cmd = args[args.index("--board") + 1 :]
    vidpid, settle, cwd = None, 3.0, None
    i = 0
    while i < len(opts):
        if opts[i] == "--vidpid":
            vidpid = opts[i + 1].lower()
        elif opts[i] == "--settle":
            settle = float(opts[i + 1])
        elif opts[i] == "--cwd":
            cwd = opts[i + 1]
        i += 2
    if not host_cmd or not board_cmd:
        sys.exit("uos-hwd-host.py: a host and a board command are required")
    vid, pid = vidpid.split(":") if vidpid else (None, None)

    before = devices(vid, pid) if vidpid else set()
    board = subprocess.Popen(board_cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, bufsize=0)
    state = {"host": None, "rc": None, "stopped": False}

    def host_side():
        if vidpid:
            while board.poll() is None:
                new = devices(vid, pid) - before
                if new:
                    break
                time.sleep(0.2)
            else:
                return
            emit(f"[host] the test's device {vidpid} enumerated; starting the host side\n")
            time.sleep(settle)
            if board.poll() is not None:
                return
        else:
            emit("[host] starting the host side\n")
        proc = subprocess.Popen(host_cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, cwd=cwd, text=True,
                                errors="replace")
        state["host"] = proc
        for line in proc.stdout:
            emit("[host] " + line)
        rc = proc.wait()
        if state["stopped"]:
            emit("[host] stopped after the board ended\n")
            return
        state["rc"] = rc
        emit(f"[host] exit {rc}\n")
        if rc != 0:
            emit(f"\nRESULT: FAIL (host side, exit {rc})\n")

    t = threading.Thread(target=host_side, daemon=True)
    t.start()

    while True:
        chunk = board.stdout.read(4096)
        if not chunk:
            break
        emit(chunk.decode("utf-8", "replace"))
    rc = board.wait()

    proc = state["host"]
    if proc is not None and proc.poll() is None:
        # The board ended first: give the host side a moment to finish its
        # last exchange, then stop it.
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            state["stopped"] = True
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
    t.join(timeout=10)

    if state["rc"] not in (None, 0):
        sys.exit(1)
    sys.exit(rc)


if __name__ == "__main__":
    main()
