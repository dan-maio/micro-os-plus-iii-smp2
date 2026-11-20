# 6b-14: the Cortex-M boards run upstream's suites through the builder

As AArch in 6b-9. Removed: each board's name-only suite folders (rtos-apis,
mutex-stress, cmsis-os-validator, fp-switch), its harness-suite.hpp verdict
wrapper and its <board>-harness-suite link interface. Added:
- platforms (nucleof411, pico, pico2, pico2-rp2350b-psram, weactf411,
  weactf412): UOS_TESTS_SUITES, UOS_TESTS_STARTUP_LIBS (semihosting,
  newlib-reent, posix-io: what the interface linked).
- board-tests/cortexm/test-end.cpp: the end of a suite on Cortex-M (one copy;
  RESULT on the console and on semihosting, then the code back to the
  kernel); each platform's include/test-end-console.h prints the line its
  way (RP2xxx: bsp/uart.hpp; Nucleo/F411: uart::init, \r\n; F412: uart::init,
  then 20 ms for the DMA) -- the user's rule: a board-specific need is a
  platform header.
- builder: platform-support.cpp optional; the Cortex-M test end; what the
  builder adds for a test itself (_test_libs: start-up, suite, storage) also
  goes into an emulated image whose libraries replace the board's
  (board_test_qemu_libs) -- without it the RP2xxx suite QEMU images lost the
  suite (caught by the command comparison).
- the -ram variants (Pico, Pico 2) stay board tests (own linker script and
  runner); their board_test_libs link the same start-up and suite targets.

Check:
- test names identical on all 40 configurations.
- hardware images: same defines, the platform include still before the
  suite's (the platform's os-app-config.h hands over with #include_next and
  adds its HW_BUILD overrides, as before). RP2xxx QEMU suite images now find
  the platform's os-app-config.h first too (before: the suite's), i.e. like
  their hardware images; outside HW_BUILD that adds only #ifndef defaults.
- build: all 14 Cortex-M configurations, every image; QEMU (debug, release):
  Pico 6/6, Pico 2 6/6, PSRAM 3/3.
- HARDWARE PROOF PENDING.
