#!/bin/bash
# run.sh: snapshot all 40 configurations into after3b/, compare with after3a/;
# build the 14 AArch/native configurations; run every portable and storage
# test (QEMU/host); list the hardware images of the storage board tests.
W=/home/dan/Desktop/micro-os-plu-iii-smp-pr-github; S=$W/step5c-check
cd $W/micro-os-plus-iii/tests
for C in $(ls build); do
  xpm run prepare --config $C > $S/after3b/$C.prepare.log 2>&1
  ninja -C build/$C -t commands > $S/after3b/$C.cmds 2>/dev/null
  ctest --test-dir build/$C -N | grep -oE 'Test +#[0-9]+: .*' | sed -E 's/Test +#[0-9]+: //' | sort > $S/after3b/$C.ctestN
done
python3 -I $S/5c3b/check.py > $S/5c3b/check.txt; tail -1 $S/5c3b/check.txt
P='(smp_test[0-4]|smp-num-test|smp-pipeline-test|smp-pro-cons-test|smp-mat-test|sd_test|smp-mat-sdcard-test|smp-shapes-test|usb_test|usb_big_test|eth-net|eth-tcp-server|wifi-net|wifi-tcp-server)-(qemu|host)$'
H='^(sd_test|smp-mat-sdcard-test|smp-num-test|smp-pipeline-test|smp-shapes-test|usb_test|usb_big_test|eth-net|eth-tcp-server|wifi-net|wifi-tcp-server|smp_test5|smp_test7|smp_test_int4)-hwd\.elf$'
for C in $(ls build | grep -E '^(aarch32-(luckfox-lyra|rpi3b|rpi-zero-2w)|aarch64-(rpi3b|rpi4b|rpi-zero-2w)|native)-'); do
  xpm run build --config $C > $S/5c3b/$C.build.log 2>&1; b=$?
  r=$(cd build/$C && timeout 7200 ctest -LE hwd -R "$P" 2>&1 | tee $S/5c3b/$C.ctest.log | grep -E "tests passed|No tests")
  h=$(ls build/$C/platform-bin/port-tests/test/ 2>/dev/null | grep -cE "$H")
  echo "$C build=$b storage-hwd-elfs=$h | $r"
  grep -E "\(Failed\)|\(Timeout\)|Not Run" $S/5c3b/$C.ctest.log | head -5
  grep -m3 -E "error:|undefined|multiple def" $S/5c3b/$C.build.log
done
exit 0
