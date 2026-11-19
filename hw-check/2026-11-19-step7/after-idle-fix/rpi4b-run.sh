#!/bin/bash
# rpi4b-run.sh (step 7, the OpenOCD route from CTest): every hwd test of the Pi 4 B (probe E661410403809139), alone:
# the 19 automatic ones, eth-net, eth-tcp-server with its client run here,
# usb_big_test with its host tool run here.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-19-step7/after-idle-fix
B=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/aarch64-rpi4b-cmake-gcc-debug
P=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/platforms/aarch64-rpi4b/tests
export UOS_HW_ADAPTER_SERIAL=E661410403809139
cd $B
ctest -L '^hwd$' -E '(usb_big_test|eth-net|eth-tcp-server)-hwd$' --output-on-failure > $H/rpi4b-batch.txt 2>&1
ctest -L '^hwd$' -R '^aarch64-rpi4b-eth-net-hwd$' --output-on-failure > $H/rpi4b-eth-net.txt 2>&1

ctest -L '^hwd$' -R '^aarch64-rpi4b-eth-tcp-server-hwd$' --output-on-failure > $H/rpi4b-eth-tcp-server.txt 2>&1 &
CT=$!
NET=$(ip -4 route get 1.1.1.1 | grep -oE 'src [0-9.]+' | awk '{print $2}'); N=${NET%.*}; IP=""
for t in $(seq 1 18); do
  sleep 5
  for i in $(seq 1 254); do ping -c1 -W1 $N.$i >/dev/null 2>&1 & done; wait $(jobs -p | grep -v "^$CT$") 2>/dev/null
  IP=$(ip neigh | grep -i "lladdr dc:a6:32:65:cc:bb" | grep -E "REACHABLE|STALE|DELAY" | awk '{print $1}' | head -1)
  [ -n "$IP" ] && break
done
echo "Pi 4 B address: ${IP:-not found}" > $H/rpi4b-eth-tcp-host.txt
[ -n "$IP" ] && timeout 120 python3 -s $P/eth-tcp-server/eth-tcp-host.py "$IP" >> $H/rpi4b-eth-tcp-host.txt 2>&1
echo "host rc=$?" >> $H/rpi4b-eth-tcp-host.txt
wait $CT

ctest -L '^hwd$' -R '^aarch64-rpi4b-usb_big_test-hwd$' --output-on-failure > $H/rpi4b-usb_big_test.txt 2>&1 &
CT=$!
for i in $(seq 1 120); do lsusb | grep -q "1d6b:0104" && break; kill -0 $CT 2>/dev/null || break; sleep 1; done
echo "gadget after ${i} s: $(lsusb | grep 1d6b:0104)" > $H/rpi4b-usb_big_host.txt
lsusb | grep -q "1d6b:0104" && timeout 120 $P/usb_big_test/host/output/usb_test_host --big 4194304 >> $H/rpi4b-usb_big_host.txt 2>&1
echo "host rc=$?" >> $H/rpi4b-usb_big_host.txt
wait $CT
