# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the Milk-V Duo (CV1800B, T-Head C906,
# RV64GC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board over JTAG.
#
# There are no `qemu` images here: QEMU does not model the CV1800B. The same
# tests run emulated on QEMU's virt machine as the platforms qemu-riscv64 and
# qemu-riscv32.
#
# hwd tests need the board, booted from its SD card with the minimal fip.bin
# (BL2 brings up the clocks, the DDR, UART4 and the JTAG pinmux; OpenSBI
# parks the hart): label `hwd` (the configuration's `test` skips them), one
# at a time. OpenOCD halts the hart, switches it to M-mode, loads the image
# into DDR and starts it; the test's semihosting SYS_EXIT ends it. Nothing is
# written to the SD card; the UART is not opened. The USB device tests need
# the PC's side (platforms/riscv-pico2/usb-host.py cdc|hid), run by hand.
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

# The board tests, built by the tests' own builder (board-tests/) from the
# board's test folder (this platform's tests/).
set (UOS_TESTS_PORT riscv)
# Upstream's suites (tests/sources/<suite>/), on the common end of a portable
# test (board-tests/portable-test-end.cpp). The board runs one hart, so they
# run under the kernel's scalar scheduler.
set (UOS_TESTS_SUITES
     blinky fp-switch mutex-stress instrumentation rtos-apis cmsis-os-validator)
# The board-only tests: this platform's tests/ (tests.cmake and one folder per
# test).
set (UOS_TESTS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

# -----------------------------------------------------------------------------
# The hardware run (the port's former test/boards/milkv-duo/hw.sh): one C906
# hart, the image at 0x80050000 (linker.ld), M-mode set before the load (the
# payload OpenSBI left runs in S-mode). There is no reset line on the header:
# each test restarts the hart at _start, so tests run back to back; a test
# that wedges the hart needs a power cycle (reset.cfg is the watchdog reset
# that works from the parked state). The Sipeed probe reads a garbage scan
# chain on every other attach after a previous session: the attach is
# retried.
include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

foreach (_app IN LISTS UOS_TESTS_APPS)
  uos_tests_add_hwd (
    NAME "${PLATFORM_NAME}-${_app}-hwd" APP "${_app}" ELF "$<TARGET_FILE:${_app}-hwd>"
    CFG "${CMAKE_CURRENT_SOURCE_DIR}/openocd.cfg" INIT
    ENTRY 0x80050000 TARGET_FMT "cv1800b.cpu%d" CORES 0 SEMIHOST_CORES 0
    RESUME entry PRELOAD "reg priv 3"
    RETRY_TRIES 3
    RETRY_IF "Examination failed|dtmcontrol is 0|Target not examined|load_image.*failed|Failed to write memory"
    RETRY_UNLESS "Examined RISC-V core"
    TIMEOUT 600
  )
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V milkv-duo: hwd tests registered (${PLATFORM_NAME})")
