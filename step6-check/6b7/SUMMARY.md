# 6b-7: smp-mat-test is a portable test (tests/sources/smp-mat-test)

Each config: xpm prepare + build, `ctest -N` compared with before-rename2
(diff 0 = same test list), then `ctest -LE hwd -R 'smp-mat-test-(qemu|host)$'`.

| config | build | ctest -N diff | result | speedup |
|---|---|---|---|---|
| aarch32-luckfox-lyra debug / release | ok | 0 | hardware only (smp-mat-test-hwd.elf built) | - |
| aarch32-rpi3b debug | ok | 0 | pass | 5.006x |
| aarch32-rpi3b release | ok | 0 | pass | 5.171x |
| aarch32-rpi-zero-2w debug | ok | 0 | pass | 5.477x |
| aarch32-rpi-zero-2w release | ok | 0 | pass | 4.452x |
| aarch64-rpi3b debug | ok | 0 | pass | 5.193x |
| aarch64-rpi3b release | ok | 0 | pass | 1.090x |
| aarch64-rpi4b debug | ok | 0 | pass | 6.317x |
| aarch64-rpi4b release | ok | 0 | pass | 3.045x |
| aarch64-rpi-zero-2w debug | ok | 0 | pass | 3.268x |
| aarch64-rpi-zero-2w release | ok | 0 | pass | 2.178x |
| native debug (N=500, B=50) | ok | 0 | pass | 1.411x |
| native release (N=500, B=50) | ok | 0 | pass | 1.233x |

Speedups under QEMU/host load are informative only; the verdict is the
block result == classical result within tolerance.

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii 47ee36c8,
aarch32 a570724, aarch64 3839bfb, posix-arch 37ab530.
