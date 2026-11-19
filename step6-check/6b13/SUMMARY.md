# 6b-13: Cortex-M board code and board tests into the kernel's tests/

As 6b-11/12, from micro-os-plus-iii-cortexm/test/:
- boards/<b>/ (board code: include/, src/, glue/, usb/, qemu/; hw.sh,
  openocd.cfg, ...) -> tests/platforms/cortexm-<b>/ (the Pi Zero and PSRAM
  boards keep building the Pico 2's board code, as before).
- boards/shared/ (hw_result.hpp, debug-host.h, syscalls.c, shared by every
  Cortex-M board) -> tests/board-tests/cortexm/ (the builder's Cortex-M
  include; uos-cortexm-board.cmake's syscalls.c).
- <b>/ -> tests/platforms/cortexm-<b>/tests/, the boards' own suite copies and
  -ram variants included for now (replaced by upstream's suites in a later
  step, with a hardware proof).
- test/hw.sh removed; the port's test/ is .gitkeep.
- Runners: board fallbacks as AArch; the Pico/Pico 2 runners call a -ram
  test's own hw.sh at $HERE/tests/<app>/hw.sh, which takes its openocd.cfg
  from $HERE/../../.
- Untracked: the Pico 2 kbd_forward binary copied (ignored like Lyra's); the
  port's untracked leftovers deleted (user), test/build-pico2/ included.
- One relative #include crossed the old layout (PSRAM fp-switch ->
  ../../pico2/harness-suite.hpp): fixed; a scan of every relative include and
  path in the moved trees finds no other.

Check:
- compare.py against ../6b12/after: all 40 configurations identical.
- ctest -N: hwd tests Nucleo 4, Pico 20, Pico 2 21, Pi Zero 14, PSRAM 15,
  F411 5, F412 5, as on hardware.
- Build: all 14, every hwd image; QEMU (debug and release): Pico 6/6,
  Pico 2 6/6, PSRAM 3/3.
