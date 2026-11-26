#!/usr/bin/env python3
"""
vf2-wifi-ram-disk-host.py - the PC side of the VisionFive 2 Lite test
vf2-wifi-ram-disk: files over Wi-Fi into the board's 512 MiB RAM disk.

One command for the whole run:

  0. start the board test with hw.sh (JTAG load into DDR; the board must be
     at the U-Boot prompt) -- skipped with --no-load if it is already running,
  1. find the board: its UDP beacon on port 5001 ("VF2-RAM-DISK <ip> 5000",
     once a second after it joined and got a DHCP lease), or -- if a host
     firewall drops broadcasts -- a scan of the local /24 for a host that
     answers the file protocol on TCP 5000,
  2. send every file to the RAM disk (TCP 5000); the board stores it, reads
     it back through FatFs and returns its CRC-32, which must match,
  3. list the RAM disk; every file must be there with its size,
  4. read every file back and save it as <file>.bak next to the original,
  5. compare SHA-256 of each <file>.bak with its original,
  6. send "verdict PASS|FAIL" -- the board prints RESULT and ends the run;
     hw.sh reports it like every other VisionFive 2 test.

UART0 is never opened (it is the user's console).

Usage (from the repository root, board at the U-Boot prompt):
    test/visionfive2-lite/vf2-wifi-ram-disk/vf2-wifi-ram-disk-host.py \\
        test.bin photo.png --random 2097152

    --no-load        the board test is already running
    --host IP        skip the beacon (and use this address)
    --build DIR      CMake build directory (default test/build-vf2-wifi)
    --keep           leave the files on the RAM disk

Exit status: 0 when the transfers verified AND hw.sh reported PASS.

Copyright (c) 2026 Dan. MIT license.
"""

import argparse
import hashlib
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]          # the repository
sys.path.insert(0, str(HERE))
import wifi_fs                  # noqa: E402

BEACON_PORT = 5001

# What this runner drives by default; smp-net-ram-disk-host.py (the Ethernet
# test) calls main() with its own values.
DEFAULT_TEST = "vf2-wifi-ram-disk"
DEFAULT_TAG = "VF2-RAM-DISK"
DEFAULT_BUILD = ROOT / "test" / "build-vf2-wifi"
DEFAULT_BOARD = "visionfive2-lite"


def local_prefix():
    """The PC's own address on the LAN, as 'a.b.c.' (for the /24 scan)."""
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))     # no packet is sent
        own = probe.getsockname()[0]
    finally:
        probe.close()
    return own, own.rsplit(".", 1)[0] + "."


def answers_file_protocol(address):
    """True if `address` runs the RAM-disk file server (df -> TOTAL ...)."""
    try:
        with socket.create_connection((address, wifi_fs.DEFAULT_TCP_PORT), timeout=0.4) as s:
            s.settimeout(2.0)
            s.sendall(b"df\n")
            data = b""
            while b"\nOK\n" not in data and len(data) < 200:
                chunk = s.recv(200)
                if not chunk:
                    break
                data += chunk
            return data.startswith(b"TOTAL ")
    except OSError:
        return False


def scan_subnet():
    """Find the board on the local /24 by its file server (TCP 5000)."""
    from concurrent.futures import ThreadPoolExecutor
    own, prefix = local_prefix()
    candidates = [prefix + str(i) for i in range(1, 255) if prefix + str(i) != own]
    with ThreadPoolExecutor(max_workers=64) as pool:
        for address, ok in zip(candidates, pool.map(answers_file_protocol, candidates)):
            if ok:
                return address
    return None


def wait_for_board(timeout, hw=None, tag=DEFAULT_TAG):
    """Return the board's IP, or None on timeout.

    Two ways, alternated: the board's UDP broadcast on port 5001 (fast, but
    a host firewall such as ufw may drop it) and, every 15 s, a scan of the
    local /24 for a host answering the file protocol on TCP 5000 (outgoing
    connections only, so no firewall rule is needed).
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", BEACON_PORT))
    sock.settimeout(1.0)
    deadline = time.monotonic() + timeout
    next_scan = time.monotonic() + 15.0
    try:
        while time.monotonic() < deadline:
            if hw is not None and hw.poll() is not None:
                return None                 # the board run already ended
            try:
                data, _ = sock.recvfrom(256)
                fields = data.decode(errors="replace").split()
                if len(fields) >= 2 and fields[0] == tag:
                    print(f"  beacon received from {fields[1]}")
                    return fields[1]
            except socket.timeout:
                pass
            if time.monotonic() >= next_scan:
                found = scan_subnet()
                if found:
                    print(f"  found by scanning the /24 (no beacon received: "
                          f"is a firewall dropping UDP {BEACON_PORT}?)")
                    return found
                next_scan = time.monotonic() + 15.0
    finally:
        sock.close()
    return None


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def transfer(host, files, keep):
    """Steps 2-5.  Returns (board, failures, summary)."""
    failures = 0
    board = wifi_fs.Board(host)
    total, free = board.df()
    print(f"RAM disk: {total / 1048576:.1f} MiB total, {free / 1048576:.1f} MiB free\n")

    originals = {}
    up_rates = []
    for path in files:
        data = path.read_bytes()
        originals[path.name] = data
        start = time.monotonic()
        crc = board.put(path.name, data, wifi_fs.progress_printer(f"send {path.name}"))
        elapsed = max(time.monotonic() - start, 1e-6)
        up_rates.append(len(data) / 1024 / elapsed)
        print(f"  stored on the RAM disk: {len(data)} bytes, crc32 {crc:08x} "
              f"(matches), {len(data) / 1024 / elapsed:.0f} KiB/s")

    print("\nfiles on the RAM disk:")
    listing = dict(board.ls())
    for name, size in sorted(listing.items()):
        print(f"  {size:>12} {name}")
    for name, data in originals.items():
        if listing.get(name) != len(data):
            print(f"  FAIL: {name} is listed as {listing.get(name)} bytes, "
                  f"expected {len(data)}")
            failures += 1

    print()
    down_rates = []
    for path in files:
        original = originals[path.name]
        start = time.monotonic()
        received = board.get(path.name, wifi_fs.progress_printer(f"get  {path.name}"))
        elapsed = max(time.monotonic() - start, 1e-6)
        down_rates.append(len(received) / 1024 / elapsed)
        backup = path.with_name(path.name + ".bak")
        backup.write_bytes(received)
        same = sha256(received) == sha256(original)
        print(f"  saved {backup} ({len(received)} bytes, "
              f"{len(received) / 1024 / elapsed:.0f} KiB/s)")
        print(f"  SHA-256 {sha256(received)[:16]}... "
              f"{'IDENTICAL' if same else 'DIFFERENT - FAIL'}")
        if not same:
            failures += 1

    if not keep:
        for path in files:
            board.rm(path.name)

    summary = (f"{len(files)} files, up {min(up_rates):.0f}-{max(up_rates):.0f} KiB/s, "
               f"down {min(down_rates):.0f}-{max(down_rates):.0f} KiB/s, SHA-256 "
               f"{'identical' if failures == 0 else 'MISMATCH'}")
    return board, failures, summary


def main(test=DEFAULT_TEST, tag=DEFAULT_TAG, build=DEFAULT_BUILD, doc=None,
         board=DEFAULT_BOARD):
    parser = argparse.ArgumentParser(description=(doc or __doc__).split("\n\n")[0])
    parser.add_argument("files", nargs="*", help="files to send")
    parser.add_argument("--random", type=int, metavar="BYTES",
                        help="also test a generated file of this size")
    parser.add_argument("--no-load", action="store_true",
                        help="the board test is already running")
    parser.add_argument("--host", help="board IP address (skip the beacon)")
    parser.add_argument("--build", default=str(build),
                        help=f"CMake build directory with {test}-hwd")
    parser.add_argument("--beacon-timeout", type=float, default=240.0,
                        help="seconds to wait for the board (load + join)")
    parser.add_argument("--keep", action="store_true",
                        help="leave the files on the RAM disk")
    args = parser.parse_args()

    files = [Path(name) for name in args.files]
    if args.random:
        generated = Path(f"{test.replace('-', '_')}_random_{args.random}.bin")
        generated.write_bytes(os.urandom(args.random))
        files.append(generated)
    if not files:
        parser.error("give at least one file, or --random BYTES")
    if len({p.name for p in files}) != len(files):
        parser.error("two files have the same name")
    for path in files:
        if not path.is_file():
            parser.error(f"{path}: no such file")

    hw = None
    if not args.no_load:
        env = dict(os.environ, BUILD=args.build)
        hw_log = Path(args.build) / ".hw-logs" / f"{test}-host-hwsh.txt"
        hw_log.parent.mkdir(parents=True, exist_ok=True)
        print(f"loading the board test ({board} hw.sh, output in {hw_log}) ...")
        hw = subprocess.Popen(
            [str(ROOT / "test" / "boards" / board / "hw.sh"),
             test, "900"],
            stdout=open(hw_log, "w"), stderr=subprocess.STDOUT, env=env)

    start = time.monotonic()
    host = args.host
    if not host:
        print(f"waiting for the board (beacon on UDP {BEACON_PORT}, or a scan "
              f"for TCP {wifi_fs.DEFAULT_TCP_PORT}) ...")
        host = wait_for_board(args.beacon_timeout, hw, tag)
        if not host:
            print("error: no beacon from the board", file=sys.stderr)
            if hw is not None:
                if hw.poll() is None:
                    print("  (hw.sh is still running; its log will show where the "
                          "board is)", file=sys.stderr)
                else:
                    print(f"  hw.sh already ended with status {hw.returncode}",
                          file=sys.stderr)
            return 1
        print(f"board on the network at {host} "
              f"({time.monotonic() - start:.1f} s after the start)\n")

    failures = 0
    summary = ""
    board = None
    try:
        board, failures, summary = transfer(host, files, args.keep)
    except (wifi_fs.BoardError, OSError) as error:
        failures += 1
        summary = f"transfer error: {error}"
        print(f"error: {error}", file=sys.stderr)

    passed = failures == 0
    try:
        if board is None:
            board = wifi_fs.Board(host)
        board.verdict(passed, summary)
        board.close()
    except (wifi_fs.BoardError, OSError) as error:
        print(f"warning: could not send the verdict: {error}", file=sys.stderr)

    print()
    print(f"PC side: {'PASSED' if passed else 'FAILED'} -- {summary}")

    if hw is None:
        return 0 if passed else 1
    print("waiting for the board's RESULT (hw.sh) ...")
    hw.wait()
    log_tail = (Path(args.build) / ".hw-logs" /
                f"{test}-host-hwsh.txt").read_text(errors="replace")
    verdict_line = [l for l in log_tail.splitlines() if l.startswith(test)]
    print(verdict_line[-1] if verdict_line else f"hw.sh exit status {hw.returncode}")
    return 0 if passed and hw.returncode == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
