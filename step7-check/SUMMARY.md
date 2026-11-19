# Step 7: hardware tests run by CTest through OpenOCD

As upstream: CTest runs OpenOCD with the board's configuration; the test's
semihosting SYS_EXIT ends OpenOCD; the RESULT line is the verdict (PASS/FAIL/
SKIP regular expressions), TIMEOUT for a run that never ends. No shell script.

- cmake/uos-hwd.tcl: uos_hwd_run, the AArch run (test_smpl/run-hw.sh's steps:
  halt, semihosting, the board's hooks, pre-load, load_image, resume in the
  board's mode; UOS_HW_ADAPTER_KHZ from the environment still overrides the
  clock). Every AArch board had SPIN_WORDS=0 and a fixed entry, so nothing is
  read from the image.
- cmake/uos-hwd-reset.cmake: a reset that drops the debug link (the Pi's PM
  watchdog, the RK3506 global reset) in its own OpenOCD session, then the
  wait, as a CTest fixture (label hwd-reset; CTest adds it to the test it
  serves). The test is not run when the session does not reach the board.
- cmake/uos-tests-run.cmake: uos_tests_add_hwd (AArch: halt and load) and
  uos_tests_add_openocd (Cortex-M: the board's commands; program, reset,
  semihosting, run; the -ram variants load to RAM and start from the vector
  table). The newest OpenOCD xPack, else the PATH's.
- Platforms: the 6 AArch and 7 Cortex-M platforms register <test>-hwd this
  way, with the facts their hw.sh had (entry, target names, cores, resume
  mode, reset, pre-load, clock, timeouts). The Zero 2 W / Pi 3 B
  openocd-probe.cfg keeps the PROBE choice (jlink default, olimex,
  cmsis-dap).
- The old run through hw.sh stays registered as <test>-hwsh (label hwsh)
  until the OpenOCD runs are proven on the boards; the 24 hardware
  configurations' test action skips both (ctest -LE "hwd|hwsh"). To delete
  after the proof: every board's hw.sh, the -ram tests' hw.sh and
  load-ram.cfg, test_smpl/run-hw.sh, test_smpl/no-power-cycle/, the -hwsh
  registrations, spi-pipeline/check.sh (a manual transcript checker; no test
  uses it; several conditions at once are not one CTest regex).

Check (no hardware): all 40 configurations configure; build commands
identical to 6b-14 (step6-check/6b14/after); every hwd test has its hwsh twin
(Pi 4 B 22, Zero 2 W 18, Pi 3 B 15 per architecture, Lyra 21, Pico 20,
Pico 2 21, Pi Zero 14, PSRAM 15, Nucleo 4, F411 5, F412 5); the generated
commands match the hw.sh ones (Pi 4 B reset, Lyra pre-load, Pico 2 -ram,
Pico clock checked); uos-hwd.tcl parses in OpenOCD; the reset script fails
cleanly with no board; a non-hardware run executes the same tests as before
(Pi 4 B: its 32 qemu). No add_test calls a .sh except the hwsh twins; no
package.json action does.

HARDWARE PROOF PENDING (user: test at the end).
