#!/usr/bin/env python3
"""
wifi-ram-disk-host.py - the PC side of the MangoPi MQ-Pro test wifi-ram-disk:
files over Wi-Fi (RTL8723DS) into the board's 512 MiB RAM disk.

The same steps as the VisionFive 2 Lite tests, whose runner this script
calls (test/visionfive2-lite/vf2-wifi-ram-disk/vf2-wifi-ram-disk-host.py):

  0. start the board test with this board's hw.sh (xfel: DDR init, load at
     0x40000000, run; the board must be in FEL -- USB-C OTG cable, microSD
     card removed) -- skipped with --no-load,
  1. find the board: its UDP beacon on port 5001 ("MQPRO-RAM-DISK <ip> 5000"),
     or a scan of the local /24 for the file protocol on TCP 5000,
  2. send every file to the RAM disk; the board returns the CRC-32 of the file
     as read back from the disk, which must match,
  3. list the RAM disk; every file must be there with its size,
  4. read every file back as <file>.bak and compare SHA-256,
  5. send "verdict PASS|FAIL": the board records it in the RTC and resets to
     FEL; hw.sh reads it back (and the board's console log, over xfel).

UART0 is never opened.  The transfers use wifi_fs.py from that directory.

Usage (from the repository root, board in FEL):
    test/mangopi-mq-pro/wifi-ram-disk/wifi-ram-disk-host.py \\
        test.bin photo.png --random 2097152

    --no-load        the board test is already running
    --host IP        skip the search (and use this address)
    --build DIR      CMake build directory (default test/build-mqpro)
    --keep           leave the files on the RAM disk

Copyright (c) 2026 Dan. MIT license.
"""

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUNNER = (HERE.parent.parent / "visionfive2-lite" / "vf2-wifi-ram-disk"
          / "vf2-wifi-ram-disk-host.py")

spec = importlib.util.spec_from_file_location("ram_disk_runner", RUNNER)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

if __name__ == "__main__":
    try:
        sys.exit(runner.main(test="wifi-ram-disk",
                             tag="MQPRO-RAM-DISK",
                             build=runner.ROOT / "test" / "build-mqpro",
                             board="mangopi-mq-pro",
                             doc=__doc__))
    except KeyboardInterrupt:
        sys.exit(130)
