#!/bin/bash
# repeat.sh <build-folder> <test-regex> <count> <out>: run one QEMU test N
# times, each cut at 150 s (a normal run takes ~30 s); PASS/FAIL/STALL each.
B="$1"; T="$2"; N="$3"; O="$4"; : > "$O"
for i in $(seq 1 "$N"); do
  timeout 150 ctest --test-dir "$B" -R "$T" --output-on-failure --no-tests=error > "$O.run$i" 2>&1; rc=$?
  if [ $rc -eq 0 ]; then r=PASS; elif [ $rc -eq 124 ]; then r=STALL; pkill -f "qemu-system-aarch64.*$(basename $B)" ; else r=FAIL; fi
  echo "run $i $r" >> "$O"
done
echo "DONE pass=$(grep -c PASS "$O") fail=$(grep -c FAIL "$O") stall=$(grep -c STALL "$O")" >> "$O"
