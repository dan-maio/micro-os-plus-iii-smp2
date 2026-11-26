#!/usr/bin/env python3
"""
smp-net-ram-disk-host.py - the PC side of the VisionFive 2 Lite test
smp-net-ram-disk: files over ETHERNET (GMAC0) into the board's 512 MiB RAM
disk.

The same steps as vf2-wifi-ram-disk-host.py, whose code this script runs
(only the test name, the beacon tag and the build directory differ):

  0. start the board test with hw.sh (JTAG load into DDR; the board must be
     at the U-Boot prompt, its Ethernet port cabled to the LAN) -- skipped
     with --no-load,
  1. find the board: its UDP beacon on port 5001
     ("VF2-NET-RAM-DISK <ip> 5000"), or a scan of the local /24 for a host
     answering the file protocol on TCP 5000 if a firewall drops it,
  2. send every file to the RAM disk; the board returns the CRC-32 of the
     file as read back from the disk, which must match,
  3. list the RAM disk; every file must be there with its size,
  4. read every file back as <file>.bak and compare SHA-256,
  5. send "verdict PASS|FAIL": the board prints RESULT and ends the run.

The transfers use wifi_fs.py from ../vf2-wifi-ram-disk/ (the protocol is
the same over Ethernet).  UART0 is never opened.

Usage (from the repository root, board at the U-Boot prompt):
    test/visionfive2-lite/smp-net-ram-disk/smp-net-ram-disk-host.py \\
        test.bin photo.png --random 2097152

    --no-load        the board test is already running
    --host IP        skip the search (and use this address)
    --build DIR      CMake build directory (default test/build-vf2)
    --keep           leave the files on the RAM disk

Copyright (c) 2026 Dan. MIT license.
"""

import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUNNER = HERE.parent / "vf2-wifi-ram-disk" / "vf2-wifi-ram-disk-host.py"

spec = importlib.util.spec_from_file_location("ram_disk_runner", RUNNER)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

if __name__ == "__main__":
    try:
        sys.exit(runner.main(test="smp-net-ram-disk",
                             tag="VF2-NET-RAM-DISK",
                             build=runner.ROOT / "test" / "build-vf2",
                             doc=__doc__))
    except KeyboardInterrupt:
        sys.exit(130)
