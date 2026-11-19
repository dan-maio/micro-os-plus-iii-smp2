# 6b-11: AArch64 board code and board tests into the kernel's tests/

Layout as 6b-10 (user: "upstream shape"). From micro-os-plus-iii-aarch64/test/:
- boards/<b>/{include,src} -> tests/platforms/aarch64-<b>/{include,src}; the
  rest of boards/<b>/ (hw.sh, OpenOCD configs, reset.cfg, run-test.cfg,
  config.txt, net/, wifi/, hw-park/, verify-bootcard.py, hw.sh.bak) ->
  tests/platforms/aarch64-<b>/. The Pi 3 B still builds the Zero 2 W board
  code (UOS_BOARD_SRC_DIR = platforms/aarch64-rpi-zero-2w); its hw.sh calls the
  Zero 2 W's.
- <b>/ (tests.cmake, include/, src/, tests) -> tests/platforms/aarch64-<b>/tests/
- third_party/lwip -> tests/third_party/lwip (one copy; identical to AArch32's,
  which goes in 6b-12).
- test/hw.sh (the manual BOARD dispatcher) removed; the port's test/ is
  .gitkeep, as upstream cortexm's.
- uos-aarch-board.cmake: the board glob leaves out *support.cpp (the test
  start-up beside the board code in src/).
- Untracked files copied, originals left: the Wi-Fi firmware blobs, the
  usb_big_test host tools; hw-park/output (build output) left in the port.

Check:
- compare.py against ../6b10/after (moved paths mapped, a/../ collapsed first,
  object names reduced to their file name, repeated -I counted once):
  34 configurations identical; the 6 AArch64 ones differ in 4 link lines each
  (eth-net/eth-tcp-server or wifi-net/wifi-tcp-server, qemu + hwd) by object
  ORDER only: the lwIP C objects (no static constructors) now after the
  board's network objects; the order among C++ objects is unchanged.
- ctest -N: hwd tests 15 (Pi 3 B), 22 (Pi 4 B), 18 (Zero 2 W), as on hardware.
- QEMU, debug and release: Pi 3 B 24/24, Zero 2 W 26/26, Pi 4 B 31/32 each;
  the failure is the known smp-shapes-test stop on the kernel assertion
  "!interrupts::in_handler_mode ()" (BASELINE.md); repeated 4 times: 4/4 pass.
