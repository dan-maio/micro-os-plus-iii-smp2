# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the MangoPi MQ-Pro (Allwinner D1, T-Head C906, RV64GC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# The `hwd` images are built; their CTest cases are not registered yet. Until
# then a hardware run is by hand, with the port's runner:
# BUILD=<this build>/platform-bin/port-tests
# <port>/test/boards/mangopi-mq-pro/hw.sh <app>.
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

set (UOS_TESTS_PORT riscv)
# Upstream's suites (tests/sources/<suite>/), on the common end of a portable
# test (board-tests/portable-test-end.cpp). The board runs one hart. There is no debugger (the images are loaded with xfel): a test ends through the board's hw_result (tests/include/hw_result.hpp: the RESULT line on UART0, then a reset into FEL), UOS_TEST_END_HW_RESULT.
set (UOS_TESTS_SUITES blinky fp-switch mutex-stress instrumentation rtos-apis cmsis-os-validator)
set (UOS_TESTS_STARTUP_DEFINES UOS_TEST_END_HW_RESULT)
# The board-only tests: this platform's tests/ (tests.cmake and one folder per
# test).
set (UOS_TESTS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

# -----------------------------------------------------------------------------
message (STATUS "RISC-V mangopi-mq-pro: hwd images built, no CTest cases yet (${PLATFORM_NAME})")
