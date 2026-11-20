#!/usr/bin/env bash
# Step 1 check: prepare + build + test (no hardware) for every installed configuration.
OUT="$(cd "$(dirname "$0")" && pwd)"
cd "$OUT/../micro-os-plus-iii/tests" || exit 1
: > "$OUT/summary.txt"
for C in $(ls build); do
  log="$OUT/$C.log"
  { xpm run prepare --config "$C" && xpm run build --config "$C"; } > "$log" 2>&1
  b=$?
  if [ $b -ne 0 ]; then echo "$C BUILD-FAILED" >> "$OUT/summary.txt"; continue; fi
  xpm run test --config "$C" >> "$log" 2>&1
  t=$?
  grep -E '^ *[0-9]+/[0-9]+ Test +#[0-9]+: ' "$log" | sed -E 's/^ *[0-9]+\/[0-9]+ Test +#[0-9]+: ([^ ]+) .*(Passed|\*\*\*Failed|\*\*\*Not Run|\*\*\*Skipped|\*\*\*Timeout|Skipped|Failed).*/\1 \2/' > "$OUT/$C.tests"
  echo "$C rc=$t $(grep -E 'tests passed' "$log" | tail -1)" >> "$OUT/summary.txt"
done
echo DONE >> "$OUT/summary.txt"
