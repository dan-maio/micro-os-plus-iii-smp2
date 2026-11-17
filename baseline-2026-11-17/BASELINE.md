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

## Setup fixes made before the baseline (no repository change)

- `aarch64-rpi4b-cmake-gcc-debug/release` had never been `xpm install --config`'d
  (no `xpacks/.bin`): their build failed at `aarch64-none-elf-objcopy: command not
  found`. Installed; then built and tested (`rerun.sh`).
- Five AArch32 configurations (luckfox-lyra debug/release, rpi3b debug/release,
  rpi-zero-2w release) had no `xpacks/.bin` either; they built with the
  `arm-none-eabi` tools on the user PATH. Installed afterwards so later steps use the
  configuration's own tools. Not re-run (their results stand).
