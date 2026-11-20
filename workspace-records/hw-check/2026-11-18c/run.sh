#!/bin/bash
# run.sh: the Lyra's open tests (smp_test_int5; smp_test7 with its file sent
# from here and y/n typed by the user; smp_test_int4 with the user's
# kbd_forward) and the Zero 2 W (aarch32) batch less usb_test and
# wifi-tcp-server; the Zero 2 W starts 20 s after the Lyra.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18c; B=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build; LT=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/aarch32/micro-os-plus-iii-aarch32/test/luckfox-lyra/smp_test6/host/output/usb_test_host
lyra () {
  cd $B/aarch32-luckfox-lyra-cmake-gcc-debug
  ctest -L hwd -R '^aarch32-luckfox-lyra-smp_test_int5-hwd$' --output-on-failure > $H/lyra-int5.txt 2>&1
  ( ctest -L hwd -R '^aarch32-luckfox-lyra-smp_test7-hwd$' --output-on-failure > $H/lyra-smp_test7.txt 2>&1 ) &
  local ct=$!
  for i in $(seq 1 150); do lsusb | grep -q "2207:0019" && break; sleep 1; done
  echo "gadget after ${i} s: $(lsusb | grep 2207:0019)" > $H/lyra-smp_test7-host.txt
  sleep 3
  lsusb | grep -q "2207:0019" && timeout 120 "$LT" --big 262144 >> $H/lyra-smp_test7-host.txt 2>&1
  echo "host rc=$?" >> $H/lyra-smp_test7-host.txt
  wait $ct
  ctest -L hwd -R '^aarch32-luckfox-lyra-smp_test_int4-hwd$' --output-on-failure > $H/lyra-int4.txt 2>&1
}
zero2w () {
  sleep 20
  cd $B/aarch32-rpi-zero-2w-cmake-gcc-debug
  PROBE=cmsis-dap UOS_HW_ADAPTER_SERIAL=E46268C3571E0D36 ctest -L hwd -E '(usb_test|wifi-tcp-server)-hwd$' --output-on-failure > $H/zero2w-batch.txt 2>&1
}
lyra & zero2w & wait
