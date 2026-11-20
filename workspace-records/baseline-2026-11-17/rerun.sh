#!/usr/bin/env bash
# Run the two Pi 4 B configurations, which could not build in the first pass (tools not installed).
OUT="$(cd "$(dirname "$0")" && pwd)"
cd "$OUT/../micro-os-plus-iii/tests" || exit 1
: > "$OUT/summary-rerun.txt"
for C in aarch64-rpi4b-cmake-gcc-debug aarch64-rpi4b-cmake-gcc-release; do
  log="$OUT/$C.log"
  [ -f "$log" ] && mv "$log" "$OUT/$C.first-pass.log"
  [ -f "$OUT/$C.tests" ] && mv "$OUT/$C.tests" "$OUT/$C.first-pass.tests"
  { xpm run prepare --config "$C" && xpm run build --config "$C"; } > "$log" 2>&1
  if [ $? -ne 0 ]; then echo "$C BUILD-FAILED" >> "$OUT/summary-rerun.txt"; continue; fi
  xpm run test --config "$C" >> "$log" 2>&1
  t=$?
  grep -E '^ *[0-9]+/[0-9]+ Test +#[0-9]+: ' "$log" | sed -E 's/^ *[0-9]+\/[0-9]+ Test +#[0-9]+: ([^ ]+) .*(Passed|\*\*\*Failed|\*\*\*Not Run|\*\*\*Skipped|\*\*\*Timeout|Skipped|Failed).*/\1 \2/' > "$OUT/$C.tests"
  echo "$C rc=$t $(grep -E 'tests passed|No tests were found' "$log" | tail -1)" >> "$OUT/summary-rerun.txt"
done
echo DONE >> "$OUT/summary-rerun.txt"
