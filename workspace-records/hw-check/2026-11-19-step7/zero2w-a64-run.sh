#!/bin/bash
# zero2w-a64-run.sh (step 7, the OpenOCD route): every hwd test of the Zero 2 W
# (AArch64), "JTAG probe on Pico" E46268C3571E0D36 (PROBE=cmsis-dap): the 16
# automatic ones (wifi-net included), then wifi-tcp-server with its client run
# here (retried until the board's server answers), then usb_test with
# send_file.py --get --terminate run here.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-19-step7
B=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/aarch64-rpi-zero-2w-cmake-gcc-debug
P=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/platforms/aarch64-rpi-zero-2w/tests
export PROBE=cmsis-dap UOS_HW_ADAPTER_SERIAL=E46268C3571E0D36 UOS_HW_ADAPTER_KHZ=10000
cd $B
ctest -L '^hwd$' -E '(usb_test|wifi-tcp-server)-hwd$' --output-on-failure > $H/zero2w-a64-batch.txt 2>&1

# wifi-tcp-server: the board joins about 2 min after the start (MAC
# 88:a2:9e:80:26:77, DHCP 192.168.1.65 so far); retry the client until it
# connects or the test ends.
ctest -L '^hwd$' -R '^aarch64-rpi-zero-2w-wifi-tcp-server-hwd$' --output-on-failure > $H/zero2w-a64-wifi-tcp-server.txt 2>&1 &
CT=$!; : > $H/zero2w-a64-wifi-tcp-host.txt
for k in $(seq 1 60); do kill -0 $CT 2>/dev/null || break; sleep 5
  IP=$(ip neigh | grep -i "lladdr 88:a2:9e:80:26:77" | awk '{print $1}' | head -1); IP=${IP:-192.168.1.65}
  timeout 100 python3 -s $P/wifi-tcp-server/wifi-tcp-host.py "$IP" --timeout 30 >> $H/zero2w-a64-wifi-tcp-host.txt 2>&1 && { echo "client ok against $IP on attempt $k" >> $H/zero2w-a64-wifi-tcp-host.txt; break; }
done
wait $CT

# usb_test: wait for a stale gadget to go, then for the test's.
( ctest -L '^hwd$' -R '^aarch64-rpi-zero-2w-usb_test-hwd$' --output-on-failure > $H/zero2w-a64-usb_test.txt 2>&1 ) &
CT=$!
for i in $(seq 1 60); do lsusb | grep -q "1d6b:0104" || break; kill -0 $CT 2>/dev/null || break; sleep 1; done
for i in $(seq 1 150); do kill -0 $CT 2>/dev/null || break; lsusb | grep -q "1d6b:0104" && break; sleep 1; done
if kill -0 $CT 2>/dev/null; then
  echo "gadget after ${i} s: $(lsusb | grep 1d6b:0104)" > $H/zero2w-a64-usb_test-host.txt
  sleep 3
  timeout 180 python3 -s $P/usb_test/send_file.py $P/usb_test/send_file.py --get --terminate >> $H/zero2w-a64-usb_test-host.txt 2>&1
  echo "host rc=$?" >> $H/zero2w-a64-usb_test-host.txt
else echo "the test ended before its gadget came up" > $H/zero2w-a64-usb_test-host.txt; fi
wait $CT
