#!/bin/bash
# build-all.sh: build every configuration after the clean-up, then run the
# native (host) tests, the only configurations whose test set changed.
S=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step5c-check/cleanup
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests
cd $T
for d in build/*/; do
  C=$(basename $d); [ -f build/$C/CMakeCache.txt ] || continue
  ( export PATH=$T/build/$C/xpacks/.bin:$PATH; ninja -C build/$C > $S/build-$C.log 2>&1; echo "$C build rc=$?" )
done > $S/build-summary.txt
for C in native-cmake-sys-debug native-cmake-sys-release; do
  ( cd build/$C && ctest -LE hwd --output-on-failure > $S/test-$C.log 2>&1; echo "$C: $(grep -E 'tests passed' $S/test-$C.log)" )
done >> $S/build-summary.txt
