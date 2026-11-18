#!/bin/bash
# run.sh: 6b-8 (clock-test portable). Snapshot all 40 configurations, compare
# with 5c-3b, then build the 14 AArch/native ones and run every portable test.
W=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github; S=$W/step6-check/6b8; O=$W/step6-check/after6b8
cd $W/micro-os-plus-iii/tests
for C in $(ls build); do
  xpm run prepare --config $C > $O/$C.prepare.log 2>&1
  ninja -C build/$C -t commands > $O/$C.cmds 2>/dev/null
  ctest --test-dir build/$C -N | grep -oE 'Test +#[0-9]+: .*' | sed -E 's/Test +#[0-9]+: //' | sort > $O/$C.ctestN
done
python3 -I $S/check.py > $S/check.txt; tail -1 $S/check.txt
P='(smp_test[0-4]|smp-num-test|smp-pipeline-test|smp-pro-cons-test|smp-mat-test|sd_test|smp-mat-sdcard-test|clock-test)-(qemu|host)$'
for C in $(ls build | grep -E '^(aarch32-(luckfox-lyra|rpi3b|rpi-zero-2w)|aarch64-(rpi3b|rpi4b|rpi-zero-2w)|native)-'); do
  xpm run build --config $C > $S/$C.build.log 2>&1; b=$?
  r=$(cd build/$C && timeout 7200 ctest -LE hwd -R "$P" 2>&1 | tee $S/$C.ctest.log | grep -E "tests passed|No tests")
  e=$(ls build/$C/platform-bin/port-tests/test/ 2>/dev/null | grep -cE '^clock-test-(hwd|host)(\.elf)?$')
  echo "$C build=$b clock-test-image=$e | $r"
  grep -E "\(Failed\)|\(Timeout\)|Not Run" $S/$C.ctest.log | head -5
  grep -m3 -E "error:|undefined|multiple def" $S/$C.build.log
done
exit 0
