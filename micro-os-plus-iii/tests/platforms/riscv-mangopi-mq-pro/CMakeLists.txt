# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the MangoPi MQ-Pro (Allwinner D1, T-Head C906, RV64GC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# hwd tests need the board in FEL mode: label `hwd` (the configuration's
# `test` skips them), one at a time. There is no debugger: CTest runs the
# board's loader (cmake/xfel-run.cmake: xfel writes the image into DDR and
# starts it; the test resets into FEL with its verdict in the RTC), which
# prints the RESULT line. The UART is not opened. The Wi-Fi tests need an
# access point (WIFI_TEST_SSID, WIFI_TEST_PASS).
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
# The hardware run.
include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

find_program (UOS_XFEL xfel)
if (NOT UOS_XFEL)
  set (UOS_XFEL "xfel")
  message (WARNING "${PLATFORM_NAME}: xfel is not installed; the hwd tests cannot run.")
endif ()

# How long a test gets to reset into FEL (the former hw.sh's SECS).
set (_secs 180)
foreach (_app IN LISTS UOS_TESTS_APPS)
  # semihosting-test is QEMU-only (BOARD_TEST_QEMU_ONLY): no hardware image.
  if (NOT TARGET "${_app}-hwd")
    continue ()
  endif ()
  set (_name "${PLATFORM_NAME}-${_app}-hwd")
  add_test (NAME "${_name}"
    COMMAND "${CMAKE_COMMAND}"
            "-DXFEL=${UOS_XFEL}" "-DOBJCOPY=${CMAKE_OBJCOPY}"
            "-DREADELF=${CMAKE_READELF}" "-DELF=$<TARGET_FILE:${_app}-hwd>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${_name}" "-DSECS=${_secs}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xfel-run.cmake")
  # The resets around the run take up to a minute each.
  math (EXPR _timeout "${_secs} + 180")
  set_tests_properties ("${_name}" PROPERTIES
    LABELS "hwd" TIMEOUT ${_timeout} RUN_SERIAL TRUE
    ${UOS_TESTS_RESULT_PROPERTIES})
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V mangopi-mq-pro: hwd tests registered (${PLATFORM_NAME})")
