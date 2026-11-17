#!/usr/bin/env bash
# repeat.sh <build-folder> <test-regex> <count> <out-file>: run one CTest test N times, count the results.
B="$1"; T="$2"; N="$3"; O="$4"
: > "$O"
for i in $(seq 1 "$N"); do
  if ctest --test-dir "$B" -R "$T" --output-on-failure --no-tests=error > "$O.run$i" 2>&1; then r=PASS; else r=FAIL; fi
  echo "run $i $r" >> "$O"
done
echo "DONE pass=$(grep -c PASS "$O") fail=$(grep -c FAIL "$O")" >> "$O"
