# 6b-9b: native's 1-CPU leg is upstream's suites (test::<suite>)

native: UOS_TESTS_SUITES mutex-stress rtos-apis, built at OS_NCPU=1 (the
port's non-SMP branch); native's copies (mutex-stress/, rtos-apis/) deleted.
Port differences handled as board facts, not copies: libucontext's C sources
compiled with -U_GNU_SOURCE (the port sets it; glibc's REG_* enum collides
with libucontext's macros); rtos-apis' board heap 96 MiB (its Chan FatFs
RAM disks reach ~34 MB; the board's default is 32 MiB). rtos-apis now runs
its Chan FatFs cases (fat12/16/32, exFAT, 1024/4096 sectors), which the copy
left out. smp-rtos-apis on upstream's sources and configuration. The
native start-up skips the SMP bring-up without OS_USE_SMP_SCHEDULER;
uos_add_app accepts sources from libraries only.

Found on the way: ctest runs a stale binary when its build failed. Every
run here deleted the three binaries first.

Check: all 40 configurations against 6b-9a (check.txt): 38 identical, native
only the three suites' commands; ctest -N identical. All native tests, both
configurations: 21/21 (native-cmake-sys-*.ctest.log).

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii
d8b36424, posix-arch ff1813a.
