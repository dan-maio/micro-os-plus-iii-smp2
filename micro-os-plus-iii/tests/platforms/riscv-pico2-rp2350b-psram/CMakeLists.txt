# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the WeAct RP2350B (16 MB flash + 8 MB PSRAM, 2x Hazard3).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# The `hwd` images are built; their CTest cases are not registered yet. Until
# then a hardware run is by hand, with the port's runner:
# BUILD=<this build>/platform-bin/port-tests
# <port>/test/boards/pico2-rp2350b-psram/hw.sh <app>.
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

set (UOS_TESTS_PORT riscv)
# Upstream's suites (tests/sources/<suite>/), on the common end of a portable
# test (board-tests/portable-test-end.cpp). The suite runs on one hart.
set (UOS_TESTS_SUITES fp-switch)
# The board-only tests: this platform's tests/ (tests.cmake and one folder per
# test).
set (UOS_TESTS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

# -----------------------------------------------------------------------------
message (STATUS "RISC-V pico2-rp2350b-psram: hwd images built, no CTest cases yet (${PLATFORM_NAME})")
