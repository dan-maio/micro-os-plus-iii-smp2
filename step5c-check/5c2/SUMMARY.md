# 5c-2: sd_test and smp-mat-sdcard-test portable, on posix-io + Chan FatFs

`run.sh`: all 40 configurations prepared and snapshotted (`../after2/`, not in
git), compared with `../before/` (the state before 5c) by `check.py`
(`check.txt`): 28 configurations identical; in the 12 of the six SD boards
every changed command belongs to an `sd_test-*` or `smp-mat-sdcard-test-*`
target; `ctest -N` identical everywhere (same test names).

Then the 12 SD configurations built and both tests run under QEMU
(`run.txt`, the ctest logs):

| board | debug | release |
|---|---|---|
| aarch32 Luckfox Lyra | built (2 hwd ELFs, no QEMU) | built |
| aarch32 rpi3b | both pass | both pass |
| aarch32 rpi-zero-2w | both pass | both pass |
| aarch64 rpi3b | both pass | both pass |
| aarch64 rpi4b | both pass | both pass |
| aarch64 rpi-zero-2w | both pass | both pass |

sd_test: host seed.bin verified, four files written/read back/listed in
/sd/tests. smp-mat-sdcard-test: A/b stored and reloaded, block == classical,
residuals ~1e-6, solutions round-tripped. (run.sh's exit code 1 is its last
`[ $b -ne 0 ] && ...` line, not a failure.)

Hardware runs pending (one at a time with the user): both tests on Lyra,
Pi 3 B and Zero 2 W (AArch32 and AArch64), Pi 4 B.

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii 4ebcd551,
aarch32 6ed74a0, aarch64 2a2ae88.
