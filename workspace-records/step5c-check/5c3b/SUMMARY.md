# 5c-3b: the storage board tests on posix-io + Chan FatFs

Converted (each to /sd/tests/ through µOS++ POSIX I/O; no test formats a card):

| board | test / app | card |
|---|---|---|
| Pi 4 B (aarch64) | smp-shapes-test, usb_big_test, eth_app (eth-net, eth-tcp-server) | board's /dev/sd |
| Zero 2 W (aarch32, aarch64) | wifi_app (wifi-net, wifi-tcp-server) | SDHOST (`devices-sdhost-posix`): the Arasan is the radio's |
| Pi 3 B, Zero 2 W (aarch32, aarch64) | usb_test: one PosixSink (long names, no '/') + `devices-usb` | board's /dev/sd |
| Lyra | smp_test5, smp_test7 | board's /dev/sd |
| Lyra | smp_test_int4 | its own interrupt-driven SdMmc as /dev/sd (OWN) |

Infrastructure: board tests may join UOS_TESTS_STORAGE (call
uos_test_storage_start(); get UOS_TESTS_STARTUP_LIBS for the start-up's
printf); `board_test_sd_posix` picks a test's SD set (OWN = the test's own
uos_test_storage_card()); portable-test-storage.cpp (mount) split from
portable-test-storage-sd.cpp (the board's /dev/sd); weak bare-metal
_gettimeofday for Chan FatFs. Lyra's C++ FatFs copy (fatfs-cpp) deleted.
smp-shapes-test: the log file is created before the heap baseline (posix-io
keeps a closed file's object until the next open: 720 B).

Check (`run.sh`, `check.py` -> `check.txt`): all 40 configurations against
5c-3a (`../after3a/`): only the converted and portable tests' commands
changed; ctest -N only gained rpi4b smp-shapes-test's QEMU card fixtures.
Build + every portable and storage test under QEMU/host (`run.txt`): all 14
built; rpi3b 20/20 x4, rpi-zero-2w 22/22 x4, rpi4b 25/25 x2, native 13/13
x2; storage hardware images all built (Lyra 7, rpi3b 5, rpi-zero-2w 7,
rpi4b 8).

Hardware-only, proven by building only until the hardware session:
usb_big_test, eth-net, eth-tcp-server, wifi-net, wifi-tcp-server, usb_test
(on the boards), smp_test5, smp_test7, smp_test_int4.

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii 4858c313,
aarch32 d64ce92, aarch64 28a49d9, devices-bcm283x 692219a. A Wi-Fi check
after the commit matched only the CMake reference "${WIFI_TEST_PASS}" and
the documented placeholders: no value is in any commit.
