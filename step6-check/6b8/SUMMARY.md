# 6b-8: clock-test is a portable test (tests/sources/clock-test)

One copy; each platform's include/reference-clock.h is the independent
reference (BCM2837/BCM2711 System Timer, Lyra's raw generic-timer counter,
the host's CLOCK_MONOTONIC) and its tolerances. Portable tests see their
platform's include/ (upstream's model: blinky's blink-led.h). Native keeps
its 5 ms wake limit (the POSIX port's cycles_per_tick() is the microseconds
since its previous call, not the tick period) and gains the 5000-tick check.

Check (run.sh, check.py -> check.txt): all 40 configurations against 5c-3b:
only the portable tests' commands changed; ctest -N identical. All 14
AArch/native configurations built; every portable test, clock-test
included, passed (run.txt), except one smp-pipeline-test stall on
aarch64-rpi3b release.

That stall is pre-existing (prove-pipeline/): the new code stalled again in
12 repeats (once, at 28 s); the pre-alignment code (wt-smp, flatfs) on the
same machine and configuration failed 2 in 24 (one QEMU exit at 21 s with no
RESULT, one hang past 150 s). Recorded in BASELINE.md as the same mid-run
kernel/port fault as the zero-2w stall.

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii
c41ba1c7, aarch32 7f9e3c3, aarch64 b809592, posix-arch 7a1b37c.
