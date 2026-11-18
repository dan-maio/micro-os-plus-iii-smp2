#!/bin/bash
# usb-test-a64.sh: usb_test on the Zero 2 W (AArch64), its host side run here:
# wait for the gadget 1d6b:0104 while the test runs, then
# send_file.py send_file.py --get --terminate.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18d
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/aarch64-rpi-zero-2w-cmake-gcc-debug
TOOL=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/aarch64/micro-os-plus-iii-aarch64/test/rpi-zero-2w/usb_test/send_file.py
( cd $T && PROBE=cmsis-dap UOS_HW_ADAPTER_SERIAL=E46268C3571E0D36 ctest -L hwd -R '^aarch64-rpi-zero-2w-usb_test-hwd$' --no-tests=error --output-on-failure > $H/zero2w-a64-usb_test-2.txt 2>&1 ) &
CT=$!
for i in $(seq 1 60); do lsusb | grep -q "1d6b:0104" || break; kill -0 $CT 2>/dev/null || break; sleep 1; done
for i in $(seq 1 150); do kill -0 $CT 2>/dev/null || break; lsusb | grep -q "1d6b:0104" && break; sleep 1; done
if ! kill -0 $CT 2>/dev/null; then echo "the test ended before its gadget came up" > $H/zero2w-a64-usb_test-2-host.txt; wait $CT; exit 0; fi
echo "gadget after ${i} s: $(lsusb | grep 1d6b:0104)" > $H/zero2w-a64-usb_test-2-host.txt
sleep 3
lsusb | grep -q "1d6b:0104" && timeout 180 python3 -s "$TOOL" "$TOOL" --get --terminate >> $H/zero2w-a64-usb_test-2-host.txt 2>&1
echo "host rc=$?" >> $H/zero2w-a64-usb_test-2-host.txt
wait $CT
