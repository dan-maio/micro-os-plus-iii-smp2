# Documentation update (before the step 7 hardware proof)

User: "before full tests on hwd, update all documentation (md/pdf), with new
config, builds, tests run". Choice: current guides rewritten; the documents
that record past work or the old design moved, unchanged but for a note, to
the kernel's docs/history/.

- Moved to docs/history/ (with their PDFs, a "History" note under the title):
  test-smpl, tests-in-aarch32-aarch64, aarch32-second-board,
  AARCH32-AARCH64-TEST-COMMANDS, AARCH32-RPI-ZERO-2W-TESTS,
  WORK-SMP-AARCH32-AARCH64-HARNESS-GUIDE, new-tests-diff,
  XPM-LINK-VS-SIBLING-LAYOUT, HARNESS-TESTS-PARADIGM,
  HARNESS-BOARD-TEST-CHEATSHEET, STEPS. Links to them repointed (31), their
  own links to the documents left behind repaired.
- Rewritten: docs/tests/COMMANDS.md, TESTS-DEVELOPER-GUIDE.md,
  docs/building-aarch32-aarch64.md; RPI4B-TESTS-HOWTO.md §0-7 (§8 kept as a
  dated record).
- Corrected: TESTS-CATALOG.md (counts and lists from ctest -N of every
  configuration; Pi 4 B section added; the F411 boards have no QEMU cases;
  probe table paths; latest results), posix-arch-port.md (layout, board,
  build, tests, running, recipes, tables, worked examples), cortexm-port.md,
  STATUS.md (a "Today 2026-11-19" section; the older sections declared a
  dated log), smp-how-it-works.md, TESTS-XPACK-SYSTEM.md, test-framework.md,
  RPI-ZERO-2W- and RPI4B-HARDWARE-STEPS.md, README-DEVELOPER/MAINTAINER scope
  notes, CLAUDE.md (the fork's platforms and tests), the per-test READMEs
  (usb_test x4, the -ram tests x8, usb_big_test, spi-pipeline, wifi x2, net,
  pico2 usb/README-tinyusb), the Cortex-M port's README, the RK3506
  package's sdcard.md.
- CMake comments in the platforms that still described the ports' builders,
  "hw.sh runs the hwd cases" or one test per power cycle (comment lines only;
  every other line checked identical; two configurations reconfigured, same
  test counts). The comments about the -hwsh twins stay until they go.
- PDFs regenerated for every changed document that had one (md2pdf.sh here:
  pandoc -> HTML -> WeasyPrint with WHY-SMP-BUILD-TEST-DIFFERS.css, A4).
- Links: every relative link in the changed documents checked; the remaining
  broken ones are pre-existing `micro-os-plus-iii.git/...` pseudo-paths.

Not touched: docs/smp-integration/, docs/specs/ (history notes).
