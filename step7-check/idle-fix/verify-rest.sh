#!/bin/bash
# verify-rest.sh: verify.sh for the configurations its first run (stopped by a
# session restart) did not reach; then the AArch second pass.
O=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step7-check/idle-fix
T=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/build
for d in $T/*/; do
  c=$(basename $d); [ -f $d/build.ninja ] || continue
  grep -q "^$c:" $O/summary.txt && continue
  ( cd $d && PATH=$PWD/xpacks/.bin:$PATH cmake --build . > $O/build-$c.log 2>&1 ); b=$?
  if [ $b -ne 0 ]; then echo "$c: BUILD FAILED" | tee -a $O/summary.txt; continue; fi
  n=$(cd $d && ctest -N -LE "hwd|hwsh" | grep -c "Test *#")
  if [ "$n" -eq 0 ]; then echo "$c: built, no non-hardware tests" | tee -a $O/summary.txt; continue; fi
  ( cd $d && PATH=$PWD/xpacks/.bin:$PATH ctest -LE "hwd|hwsh" --output-on-failure > $O/test-$c.log 2>&1 )
  echo "$c: $(grep -E 'tests passed|tests failed' $O/test-$c.log | tail -1)" | tee -a $O/summary.txt
done
echo ALL DONE | tee -a $O/summary.txt
/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step7-check/isr-fix/verify.sh
