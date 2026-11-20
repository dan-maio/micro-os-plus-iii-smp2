#!/bin/bash
# run.sh: build the six AArch64 configurations (old images deleted first, so
# ctest cannot run a stale one) and run their non-hardware tests.
S=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step6-check/6b11
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests
for b in rpi3b rpi4b rpi-zero-2w; do for v in debug release; do
  C=aarch64-$b-cmake-gcc-$v
  ( cd $T; export PATH=$T/build/$C/xpacks/.bin:$PATH
    find build/$C/platform-bin -name '*.elf' -delete
    ninja -C build/$C > $S/build-$C.log 2>&1; rc=$?
    cd build/$C && ctest -LE hwd --output-on-failure > $S/test-$C.log 2>&1
    echo "$C build rc=$rc: $(grep -E 'tests passed' $S/test-$C.log)" )
done; done > $S/summary.txt
