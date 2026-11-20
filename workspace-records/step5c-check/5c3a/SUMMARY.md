# 5c-3a: the storage start-up mounts /sd/; smp-num-test and smp-pipeline-test on posix-io

- `board-tests/portable-test-storage.cpp` mounts the card's first FAT
  partition at /sd/ and creates /sd/tests/ before os_main()
  (`uos_test_storage_start()`, a weak hook `portable-test-end.cpp` calls);
  sd_test and smp-mat-sdcard-test lost their own mount code.
- smp-num-test (/sd/tests/num.txt, appended with O_APPEND, no RAM copy) and
  smp-pipeline-test (/sd/tests/pipe.dat, read back with lseek) on posix-io;
  in UOS_TESTS_STORAGE on the six SD boards and native; QEMU and host cards
  are MBR + FAT32 images (sd-image.py).

Check (`run.sh`, `check.py` -> `check.txt`): all 40 configurations, against
5c-2 (`../after2/`): only the four storage tests' commands changed; ctest -N
only gained native's two host card fixtures (`-sd-make`, `-sd-remove`).

Every portable test (smp_test0-4, smp-num, smp-pipeline, smp-pro-cons,
smp-mat, sd_test, smp-mat-sdcard) on the 14 AArch/native configurations
(`run.txt`): all built; 19/19 pass on each of the 10 QEMU configurations,
13/13 on native debug and release; Lyra build-only (no QEMU).

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii 83aefc05,
aarch32 a2ac4fb, aarch64 febe668, posix-arch 2dd15c3.
