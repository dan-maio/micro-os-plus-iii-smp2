# 5c-1: the SD card as a posix-io block device (no test uses it yet)

New targets, the old ones unchanged:

- devices-storage: `micro-os-plus::devices-storage-sd` (sd.hpp only),
  `micro-os-plus::devices-storage-posix` (`sd::block_device`, over the
  kernel's posix-io);
- devices-bcm283x: `micro-os-plus::devices-sd-posix`;
- devices-rk3506: `micro-os-plus::devices-rk3506-sd-posix`;
- posix-arch: `micro-os-plus::devices-hostfile-sd`;
- kernel tests: `UOS_TESTS_STORAGE` in the builder, `UOS_BOARD_SD_POSIX` in
  the 7 SD platforms, `board-tests/portable-test-storage.cpp` (/dev/sd);
  native links devices-storage, Lyra gains the chan-fatfs folder.

Check (`snap.sh before` with the changes stashed, `snap.sh after` with them,
native `link-deps` re-run): `ninja -t commands` and `ctest -N` of all 40
configurations byte-for-byte identical (before.txt, after.txt: all
prepare=0). The snapshots themselves (before/, after/, 114 MB each) are not
kept in git.

Commits (align/smp-upstream, pushed to local): devices-storage f7524c1,
devices-bcm283x 7596f61, devices-rk3506 7b02831, posix-arch 0d77ebd,
micro-os-plus-iii ba2e926d.
