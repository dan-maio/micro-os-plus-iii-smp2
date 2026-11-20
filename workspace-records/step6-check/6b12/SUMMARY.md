# 6b-12: AArch32 board code and board tests into the kernel's tests/

As 6b-11, from micro-os-plus-iii-aarch32/test/:
- boards/<b>/{include,src} -> tests/platforms/aarch32-<b>/{include,src}; the
  rest of boards/<b>/ (hw.sh, OpenOCD configs, Lyra's usb/, the Zero 2 W's
  wifi/ with its tracked firmware, qemu-raspi3-shim/, ...) ->
  tests/platforms/aarch32-<b>/. The Pi 3 B builds the Zero 2 W board code;
  its hw.sh calls the Zero 2 W's.
- <b>/ -> tests/platforms/aarch32-<b>/tests/ (Lyra's _port_test_dir too).
- third_party/lwip removed: identical to tests/third_party/lwip (6b-11).
- test/hw.sh removed; the port's test/ is .gitkeep.
- Untracked Lyra host tools copied (smp_test6/host/output, smp_test_int4's
  kbd_forward), originals left.
- Comments describing the old layout (the port owns its tests, the
  per-board harness-suite wrappers) rewritten.

Check:
- compare.py against ../6b11/after: 38 configurations identical; the 2 Zero 2 W
  ones differ in the 4 Wi-Fi link lines by object ORDER only (lwIP C objects),
  as on AArch64.
- ctest -N: hwd tests 21 (Lyra), 15 (Pi 3 B), 18 (Zero 2 W), as on hardware.
- Build: all six; Lyra 21 images for its 21 tests (mutex-stress-test has no
  .elf suffix, as before).
- QEMU, debug and release: Pi 3 B 24/24, Zero 2 W 26/26.
