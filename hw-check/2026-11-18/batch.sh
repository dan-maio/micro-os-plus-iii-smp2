#!/bin/bash
# batch.sh: every hwd test of the Lyra and of the Pico 2, each board's one
# after another, the two boards in parallel (debug builds).
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-18; T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build
( cd $T/aarch32-luckfox-lyra-cmake-gcc-debug && ctest -L hwd -E 'smp_test0-hwd$' --output-on-failure > $H/lyra-batch.txt 2>&1 ) &
( cd $T/cortexm-pico2-cmake-gcc-debug && UOS_HW_ADAPTER_SERIAL=E6633861A34C2E38 ctest -L hwd -E 'smp-test0-hwd$' --output-on-failure > $H/pico2-batch.txt 2>&1 ) &
wait
