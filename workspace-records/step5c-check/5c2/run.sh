#!/bin/bash
# run.sh: snapshot all 40 configurations into after2/, compare, then build the
# SD boards and run sd_test + smp-mat-sdcard-test under QEMU.
W=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github; S=$W/step5c-check
cd $W/micro-os-plus-iii/tests
for C in $(ls build); do
  xpm run prepare --config $C > $S/after2/$C.prepare.log 2>&1
  ninja -C build/$C -t commands > $S/after2/$C.cmds 2>/dev/null
  ctest --test-dir build/$C -N | grep -oE 'Test +#[0-9]+: .*' | sed -E 's/Test +#[0-9]+: //' | sort > $S/after2/$C.ctestN
done
python3 -I $S/5c2/check.py > $S/5c2/check.txt; tail -1 $S/5c2/check.txt
for C in $(ls build | grep -E '^aarch(32-(luckfox-lyra|rpi3b|rpi-zero-2w)|64-(rpi3b|rpi4b|rpi-zero-2w))-'); do
  xpm run build --config $C > $S/5c2/$C.build.log 2>&1; b=$?
  r=$(cd build/$C && timeout 3000 ctest -LE hwd -R '(sd_test|smp-mat-sdcard-test)-qemu$' -V 2>&1 | tee $S/5c2/$C.ctest.log | grep -E "tests passed|No tests")
  v=$(grep -oE "sd_test: [A-Za-z ]+|verdict: [a-z]+|round-trip to/from SD: [A-Z]+" $S/5c2/$C.ctest.log | tr '\n' ' ')
  e=$(ls build/$C/platform-bin/port-tests/test/ | grep -cE '^(sd_test|smp-mat-sdcard-test)-hwd.elf$')
  echo "$C build=$b hwd-elfs=$e | $r | $v"
  [ $b -ne 0 ] && grep -m4 -E "error:|undefined|multiple definition" $S/5c2/$C.build.log
done
