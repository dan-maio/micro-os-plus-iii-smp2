#!/bin/bash
# The Pico (RP2040, Debug Probe; pico-run.sh drives its USB tests) and the
# Nucleo F411 (ST-LINK) in parallel; the second start staggered.
H=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/hw-check/2026-11-19
$H/pico-run.sh &
sleep 20
( cd /home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build/cortexm-nucleof411-cmake-gcc-debug && ctest -L hwd --output-on-failure > $H/nucleof411-batch.txt 2>&1 ) &
wait
