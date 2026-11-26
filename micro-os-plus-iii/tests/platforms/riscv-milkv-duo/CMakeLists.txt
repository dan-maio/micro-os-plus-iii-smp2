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
# The `hwd` images are built; their CTest cases (OpenOCD from CTest, as the
# other boards) are not registered yet. Until then a hardware run is by hand,
# with the port's runner: BUILD=<this build>/platform-bin/port-tests
# <port>/test/boards/milkv-duo/hw.sh <app>.
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
message (STATUS "RISC-V milkv-duo: hwd images built, no CTest cases yet (${PLATFORM_NAME})")
