#!/bin/bash
# run.sh: snapshot all 40 configurations into after3a/, compare with after2/,
# then build the 14 AArch/native configurations and run every portable test.
W=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github; S=$W/step5c-check
cd $W/micro-os-plus-iii/tests
for C in $(ls build); do
  xpm run prepare --config $C > $S/after3a/$C.prepare.log 2>&1
  ninja -C build/$C -t commands > $S/after3a/$C.cmds 2>/dev/null
  ctest --test-dir build/$C -N | grep -oE 'Test +#[0-9]+: .*' | sed -E 's/Test +#[0-9]+: //' | sort > $S/after3a/$C.ctestN
done
python3 -I $S/5c3a/check.py > $S/5c3a/check.txt; tail -1 $S/5c3a/check.txt
P='(smp_test[0-4]|smp-num-test|smp-pipeline-test|smp-pro-cons-test|smp-mat-test|sd_test|smp-mat-sdcard-test)-(qemu|host)$'
for C in $(ls build | grep -E '^(aarch32-(luckfox-lyra|rpi3b|rpi-zero-2w)|aarch64-(rpi3b|rpi4b|rpi-zero-2w)|native)-'); do
  xpm run build --config $C > $S/5c3a/$C.build.log 2>&1; b=$?
  r=$(cd build/$C && timeout 7200 ctest -LE hwd -R "$P" 2>&1 | tee $S/5c3a/$C.ctest.log | grep -E "tests passed|No tests")
  echo "$C build=$b | $r"
  grep -E "\(Failed\)|\(Timeout\)|Not Run" $S/5c3a/$C.ctest.log | head -5
done
exit 0
