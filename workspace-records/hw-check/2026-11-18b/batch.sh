#!/bin/bash
# batch.sh: every hwd test of the Pi 4B (except the three that need the user:
# usb_big_test, eth-net, eth-tcp-server) and of the WeAct F411, each board's
# one after another, the two boards in parallel (debug builds).
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18b; T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build
( cd $T/aarch64-rpi4b-cmake-gcc-debug && UOS_HW_ADAPTER_SERIAL=E661410403809139 ctest -L hwd -E '(usb_big_test|eth-net|eth-tcp-server)-hwd$' --output-on-failure > $H/rpi4b-batch.txt 2>&1 ) &
( cd $T/cortexm-weactf411-cmake-gcc-debug && ctest -L hwd --output-on-failure > $H/weactf411-batch.txt 2>&1 ) &
wait
