#!/usr/bin/env bash
# compare.sh <config>...: compile commands, all ninja commands and ctest -N against step4-check/before/.
H="$(cd "$(dirname "$0")" && pwd)"; T="$H/../micro-os-plus-iii/tests"; N="$T/xpacks/.bin/ninja"
for C in "$@"; do
  B="$T/build/$C"
  cc=$(python3 - "$H/before/$C.cc.json" "$B/compile_commands.json" <<'PY'
import json,sys
a={e['file']+'|'+e.get('output',''):e['command'] for e in json.load(open(sys.argv[1]))}
b={e['file']+'|'+e.get('output',''):e['command'] for e in json.load(open(sys.argv[2]))}
d=[k for k in set(a)|set(b) if a.get(k)!=b.get(k)]
print(f"{len(b)} entries, {len(d)} differ")
PY
)
  $N -C "$B" -t commands 2>/dev/null > "$H/$C.ninja-cmds.after"
  nd=$(diff <(sort "$H/before/$C.ninja-cmds") <(sort "$H/$C.ninja-cmds.after") | grep -c '^[<>]')
  ctest --test-dir "$B" -N | grep -oE 'Test +#[0-9]+: .*' | sed -E 's/Test +#[0-9]+: //' | sort > "$H/$C.ctestN.after"
  td=$(diff "$H/before/$C.ctestN" "$H/$C.ctestN.after" | grep -c '^[<>]')
  echo "$C: compile_commands $cc; ninja commands $(wc -l < "$H/$C.ninja-cmds.after"), $nd lines differ; ctest -N $(wc -l < "$H/$C.ctestN.after"), $td differ"
done
