#!/bin/bash
# run.sh: build the 14 Cortex-M board configurations (old images deleted
# first) and run their non-hardware tests (Pico, Pico 2, PSRAM have some).
S=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step6-check/6b14
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests
for b in nucleof411 pico pico2 pico2-pizero pico2-rp2350b-psram weactf411 weactf412; do for v in debug release; do
  C=cortexm-$b-cmake-gcc-$v
  ( cd $T; export PATH=$T/build/$C/xpacks/.bin:$PATH
    find build/$C/platform-bin -name '*.elf' -delete
    ninja -C build/$C > $S/build-$C.log 2>&1; rc=$?
    n=$(find build/$C/platform-bin -name '*-hwd.elf' | wc -l)
    cd build/$C && ctest -LE hwd --output-on-failure > $S/test-$C.log 2>&1
    echo "$C build rc=$rc, $n hwd images: $(grep -E 'tests passed|No tests were found' $S/test-$C.log)" )
done; done > $S/summary.txt
