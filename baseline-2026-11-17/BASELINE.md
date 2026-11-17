# Baseline, 2026-11-17 — branch `smp`, unchanged

Repositories at: micro-os-plus-iii d75552cd, -aarch32 b79d35d, -aarch64 cb16868,
-cortexm e0a7ceb, -posix-arch dcd8099.

Every installed configuration (40) under `micro-os-plus-iii/tests/build/`:
`xpm run prepare`, `build`, `test` (non-hardware tests only; `ctest -LE hwd`
where the configuration's `test` action says so). Script: `run.sh`; the two Pi 4 B
configurations: `rerun.sh`.

| | Configurations | Tests |
|---|---|---|
| Built, tests run | 30 | 283 passed, 1 failed |
| Built, hardware tests only (nothing to run): aarch32-luckfox-lyra, cortexm-nucleof411, cortexm-pico2-pizero, cortexm-weactf411, cortexm-weactf412 (debug + release) | 10 | — |

Per configuration: `summary.txt`, `summary-rerun.txt`, `<config>.log`, `<config>.tests`.

## Known flaky tests (existing, not caused by any step)

`aarch64-rpi4b-smp-shapes-test-qemu`, **debug** build only: QEMU exits with rc=1 and
no `RESULT:` line, at a random point (after round 14 of 20; once 0.2 s into phase 2).
Four runs: fail, pass, pass, fail. The release build passed. Treat a failure of this
test alone as "flaky, as in the baseline".

`aarch64-rpi4b-cmsis-os-validator-qemu`, debug and release: the validator ends with
`RESULT: FAIL` now and then. Found in the step 1 check; measured on the unchanged
`smp` branch (built from git worktrees under `wt-smp/`): **5 failures in 12 runs**
of the release build. The baseline's two passes were luck.

`2xcortex-m33-mutex-stress-test`, **debug** build: fails now and then, at a random
point after 20-35 s. Seen as `!assertion "!scheduler::locked ()" failed` in
`os::rtos::mutex::lock()` (src/rtos/os-mutex.cpp:1071) and as `__cxa_pure_virtual`,
then `abort()`. Found in the step 2 check; measured on the unchanged `smp` branch
(worktrees under `wt-smp/`): **1 failure in 12 runs**; the step 2 build: 1 in 13.
The compile commands and the link of both builds are identical. A real SMP race in
the kernel, to be investigated separately.

`cmsis-os-validator-qemu` on the other AArch boards (aarch32-rpi3b, aarch64-rpi3b,
aarch64-rpi-zero-2w): `RESULT: FAIL` now and then, like on the Pi 4 B. Found in the
step 2 check; 12 runs of each on `smp` and on step 2: aarch64-rpi3b debug failed 1
in 12 on both, the other two 0 in 12 on both. Compile commands identical to `smp`.
Details: `step2-check/prove/`.

`aarch64-rpi4b-smp-shapes-test-qemu`, **release** build too: seen in the step 5a
check (stops after round 10 of 20, no `RESULT:` line), the same symptom as the
debug case above.

`native-smp-pipeline-test-host`, release: once, in the step 5a check, a
segmentation fault on CPU 2 in thread `sdwrt0`, inside the POSIX port's
`port::scheduler::switch_stacks()` called from `clock::sleep_for()` (the SD
writer was sleeping; no SD access in the backtrace). The native build was
identical to step 4 (same compile and link commands); 12 repeats on it all
passed (`step5-check/prove/`). A rare race in the POSIX port's context switch,
to be investigated separately.

**AArch64 output was invisible under QEMU until step 6** (fixed in the
AArch64 port, include/semihosting.hpp): printf() and the kernel trace went
to semihosting SYS_WRITE on handle 1, which nothing opens, so QEMU dropped
them; only the SYS_WRITE0 lines (RESULT) reached the log. The "stops with no
RESULT line" failures above (smp-shapes on the Pi 4 B) were therefore
silent. With the output visible:

`aarch64-rpi4b-smp-list-storage-test-qemu`, debug: stops on the kernel
assertion `!interrupts::in_handler_mode ()` in `clock::sleep_for()`
(os-clocks.cpp:211), raised in an ordinary thread (scan8): the kernel
believes a thread runs in an interrupt handler. 3 failures in 13 runs after
the semihosting fix (it never failed in the 6 earlier runs; the extra
semihosting traps, each of which stops every core in QEMU, change the
timing). Very likely the same bug as the smp-shapes stops. A kernel/port
bug, to be investigated separately.

`aarch64-rpi-zero-2w-smp-pipeline-test-qemu`, release: once, all threads
stalled after 9.2 s (no output, no assertion); 0 in 12 repeats.

## Setup fixes made before the baseline (no repository change)

- `aarch64-rpi4b-cmake-gcc-debug/release` had never been `xpm install --config`'d
  (no `xpacks/.bin`): their build failed at `aarch64-none-elf-objcopy: command not
  found`. Installed; then built and tested (`rerun.sh`).
- Five AArch32 configurations (luckfox-lyra debug/release, rpi3b debug/release,
  rpi-zero-2w release) had no `xpacks/.bin` either; they built with the
  `arm-none-eabi` tools on the user PATH. Installed afterwards so later steps use the
  configuration's own tools. Not re-run (their results stand).
