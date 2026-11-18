#!/bin/bash
# batch.sh: every hwd test of the Zero 2 W (AArch64, CMSIS-DAP probe) and of
# the Pi 3 B (AArch64, Olimex probe), each board's one after another, the two
# boards in parallel (debug builds); the second start is staggered.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18d; T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build
( cd $T/aarch64-rpi-zero-2w-cmake-gcc-debug && PROBE=cmsis-dap UOS_HW_ADAPTER_SERIAL=E46268C3571E0D36 ctest -L hwd --output-on-failure > $H/zero2w-a64-batch.txt 2>&1 ) &
sleep 20
( cd $T/aarch64-rpi3b-cmake-gcc-debug && PROBE=olimex ctest -L hwd --output-on-failure > $H/rpi3b-a64-batch.txt 2>&1 ) &
wait
