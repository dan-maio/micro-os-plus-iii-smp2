#!/bin/bash
# snapshot.sh <out-dir>: reconfigure every build configuration and record, per
# configuration, its sorted compile commands and its link commands (ninja -t
# commands), with absolute workspace paths kept (they do not change here).
O="$1"; mkdir -p "$O"
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests
cd $T
for d in build/*/; do
  C=$(basename $d); [ -f build/$C/CMakeCache.txt ] || continue
  ( export PATH=$T/build/$C/xpacks/.bin:$PATH
    cmake -S . -B build/$C > $O/$C.configure.log 2>&1 || echo "CONFIGURE FAILED" >> $O/$C.configure.log
    ninja -C build/$C -t commands 2>/dev/null | sort > $O/$C.commands )
  echo "$C $(wc -l < $O/$C.commands) commands $(grep -c 'CONFIGURE FAILED' $O/$C.configure.log)"
done
