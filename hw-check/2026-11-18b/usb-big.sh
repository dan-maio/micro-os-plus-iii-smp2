#!/bin/bash
# usb-big.sh: usb_big_test on the Pi 4 B, driven from here: start the test,
# wait for its gadget (1d6b:0104) on the bus, send a 4 MiB pattern with the
# test's host tool (--big), then wait for the board's verdict.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18b
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/aarch64-rpi4b-cmake-gcc-debug
TOOL=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/aarch64/micro-os-plus-iii-aarch64/test/rpi4b/usb_big_test/host/output/usb_test_host
( cd $T && UOS_HW_ADAPTER_SERIAL=E661410403809139 ctest -L hwd -R '^aarch64-rpi4b-usb_big_test-hwd$' --no-tests=error -V > $H/rpi4b-usb_big_test.txt 2>&1; echo "rc=$?" >> $H/rpi4b-usb_big_test.txt ) &
CT=$!
for i in $(seq 1 120); do lsusb | grep -q "1d6b:0104" && break; sleep 1; done
echo "gadget after ${i} s: $(lsusb | grep 1d6b:0104)" > $H/rpi4b-usb_big_host.txt
lsusb | grep -q "1d6b:0104" && timeout 120 "$TOOL" --big 4194304 >> $H/rpi4b-usb_big_host.txt 2>&1
echo "host rc=$?" >> $H/rpi4b-usb_big_host.txt
wait $CT
