#!/bin/bash
# snap.sh <before|after>: prepare every configuration, save `ninja -t commands`
# and `ctest -N` into step5c-check/<stage>/.
W=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github; S=$W/step5c-check; st=$1
cd $W/micro-os-plus-iii/tests
for C in $(ls build); do
  xpm run prepare --config $C > $S/$st/$C.prepare.log 2>&1; rc=$?
  ninja -C build/$C -t commands > $S/$st/$C.cmds 2>/dev/null
  ctest --test-dir build/$C -N | grep -oE 'Test +#[0-9]+: .*' | sed -E 's/Test +#[0-9]+: //' | sort > $S/$st/$C.ctestN
  echo "$st $C prepare=$rc cmds=$(wc -l < $S/$st/$C.cmds)"
done
