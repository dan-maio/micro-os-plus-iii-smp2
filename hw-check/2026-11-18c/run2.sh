#!/bin/bash
# run2.sh: smp_test_int5 (5 beats) on the Lyra; 20 s later wifi-tcp-server on
# the Zero 2 W, its client run here once the board's address shows in the
# PC's neighbour table (a Raspberry Pi MAC other than the Pi 4 B's).
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18c; B=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build
TOOL=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/aarch32/micro-os-plus-iii-aarch32/test/rpi-zero-2w/wifi-tcp-server/wifi-tcp-host.py
( cd $B/aarch32-luckfox-lyra-cmake-gcc-debug && ctest -L hwd -R '^aarch32-luckfox-lyra-smp_test_int5-hwd$' --output-on-failure > $H/lyra-int5-5beats.txt 2>&1 ) &
sleep 20
( cd $B/aarch32-rpi-zero-2w-cmake-gcc-debug && PROBE=cmsis-dap UOS_HW_ADAPTER_SERIAL=E46268C3571E0D36 ctest -L hwd -R '^aarch32-rpi-zero-2w-wifi-tcp-server-hwd$' --output-on-failure > $H/zero2w-wifi-tcp-server.txt 2>&1 ) &
CT=$!
NET=$(ip -4 route get 1.1.1.1 | grep -oE 'src [0-9.]+' | awk '{print $2}'); P=${NET%.*}
IP=""
for t in $(seq 1 30); do
  sleep 10
  for i in $(seq 1 254); do ping -c1 -W1 $P.$i >/dev/null 2>&1 & done; wait $(jobs -p | grep -v "^$CT$") 2>/dev/null
  IP=$(ip neigh | grep -iE "lladdr (dc:a6:32|e4:5f:01|d8:3a:dd|2c:cf:67|28:cd:c1|b8:27:eb)" | grep -vi "dc:a6:32:65:cc:bb" | grep -E "REACHABLE|STALE|DELAY" | awk '{print $1}' | head -1)
  [ -n "$IP" ] && break
done
echo "zero2w address after $((t*10)) s: $IP $(ip neigh | grep "^$IP " | awk '{print $5}')" > $H/zero2w-wifi-tcp-host.txt
[ -n "$IP" ] && timeout 120 python3 -I "$TOOL" "$IP" --size 262144 >> $H/zero2w-wifi-tcp-host.txt 2>&1
echo "host rc=$?" >> $H/zero2w-wifi-tcp-host.txt
wait
