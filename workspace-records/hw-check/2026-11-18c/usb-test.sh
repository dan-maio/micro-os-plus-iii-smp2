#!/bin/bash
# usb-test.sh: usb_test on the Zero 2 W (AArch32), driven from here: start
# it, wait for its gadget 1d6b:0104, run the test's send_file.py (as the run that passed before the rewrite: one file, --get; the edge
# sizes, each read back with --get, then the terminating PING).
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18c
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/aarch32-rpi-zero-2w-cmake-gcc-debug
TOOL=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/aarch32/micro-os-plus-iii-aarch32/test/rpi-zero-2w/usb_test/send_file.py
( cd $T && PROBE=cmsis-dap UOS_HW_ADAPTER_SERIAL=E46268C3571E0D36 UOS_HW_ADAPTER_KHZ=4000 ctest -L hwd -R '^aarch32-rpi-zero-2w-usb_test-hwd$' --no-tests=error --output-on-failure > $H/zero2w-usb_test.txt 2>&1; echo "rc=$?" >> $H/zero2w-usb_test.txt ) &
CT=$!
# A gadget already on the bus is the previous run's, still running: wait for
# the reset to remove it. And wait only while the test runs: a failed reset
# ends it, and the host tool must not talk to a stale gadget.
for i in $(seq 1 60); do lsusb | grep -q "1d6b:0104" || break; kill -0 $CT 2>/dev/null || break; sleep 1; done
for i in $(seq 1 150); do kill -0 $CT 2>/dev/null || break; lsusb | grep -q "1d6b:0104" && break; sleep 1; done
if ! kill -0 $CT 2>/dev/null; then echo "the test ended before its gadget came up" > $H/zero2w-usb_test-host.txt; wait $CT; exit 0; fi
echo "gadget after ${i} s: $(lsusb | grep 1d6b:0104)" > $H/zero2w-usb_test-host.txt
sleep 3
lsusb | grep -q "1d6b:0104" && timeout 180 python3 -s "$TOOL" "$TOOL" --get --terminate >> $H/zero2w-usb_test-host.txt 2>&1
echo "host rc=$?" >> $H/zero2w-usb_test-host.txt
wait $CT
