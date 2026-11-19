# Step 7 hardware proof: the OpenOCD route from CTest

## Raspberry Pi 4 B (AArch64): 22/22 pass

JTAG probe on Pico E661410403809139, alone; `rpi4b-run.sh` (the 2026-11-19
script with `ctest -L '^hwd$'`: plain `-L hwd` also matches the reset
fixtures' `hwd-reset` label). Every test preceded by its CTest reset fixture
(PM watchdog, 10 s wait): no power cycle in the whole run.

- First attempt: every reset fixture failed in 0.02 s, OpenOCD saying
  "Can't find openocd.cfg". Bug in step 7: the reset's OpenOCD arguments
  reached uos-hwd-reset.cmake with their ';' still escaped, so OpenOCD got
  one argument. Fixed: joined with '|' and split again in the script (the
  offline check had "failed cleanly", for this wrong reason). Run stopped
  before the network and USB tests.
- smp_test0 alone (`rpi4b-smp_test0.txt`): reset 10.3 s, test 12.4 s, PASS.
- The batch (`rpi4b-batch.txt`): 18/19. `smp-list-storage-test`, the first
  test, ended 2.4 s after the resume with no semihosting output (OpenOCD
  exited; no error printed). Rerun alone with -V
  (`rpi4b-smp-list-storage-rerun.txt`): PASS in 13.3 s,
  "RESULT: PASS (96 nodes in 3 regions checked, 10 churn rounds, heap
  restored)". Open: an early end on the first test after the stopped run;
  not seen again.
- eth-net PASS (13.2 s).
- eth-tcp-server PASS (15.6 s); client here against 192.168.1.191, 262144
  bytes, CRC-32 15aff1b5, echo identical, 142 Mbit/s.
- usb_big_test PASS (19.7 s); usb_test_host --big 4194304: "ACK:
  count=4194304, status OK", all bytes match.

Faster than the hw.sh route (e.g. smp_test4 12.5 s).
