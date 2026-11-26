# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the Milk-V Duo (CV1800B, T-Head C906,
# RV64GC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd` for the probe and `-qemu` for the emulator, and this platform
# registers what it produced, named `<platform>-<app>-<variant>`.
#
# QEMU does not model the CV1800B: the `qemu` image of each test runs on QEMU's
# virt machine (RV64GC, 128 MiB, no firmware -- the image is the reset
# vector), linked against the port's stand-in micro-os-plus::riscv-qemu-virt
# (tests/tests.cmake). The console is the virt UART on stdio; each test ends
# itself through the semihosting SYS_EXIT, so CTest reads a real exit code,
# and the RESULT line it prints says FAIL or SKIP where that applies.
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

set (_port_bin "${CMAKE_CURRENT_BINARY_DIR}/port-tests")

# The QEMU binary, from the configuration's xpm .bin (the per-config pin).
unset (_qemu CACHE)
find_program (
  _qemu qemu-system-riscv64
  HINTS "${CMAKE_BINARY_DIR}/xpacks/.bin"
)
if (NOT _qemu)
  set (_qemu "qemu-system-riscv64")
endif ()

include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

# -----------------------------------------------------------------------------
# One CTest case per test the builder built with a `qemu` image.
foreach (_app IN LISTS UOS_TESTS_APPS)
  if (TARGET "${_app}-qemu")
    add_test (
      NAME "${PLATFORM_NAME}-${_app}-qemu"
      COMMAND
        "${_qemu}" -M virt -cpu rv64 -smp 1 -m 128M -nographic -bios none
        -kernel "${_port_bin}/test/${_app}-qemu.elf"
        -semihosting-config enable=on,target=native
        -monitor none -serial stdio
    )
    uos_tests_timeout ("${_app}" _timeout)
    set_tests_properties (
      "${PLATFORM_NAME}-${_app}-qemu"
      PROPERTIES LABELS "qemu" TIMEOUT ${_timeout}
                 FAIL_REGULAR_EXPRESSION "RESULT: FAIL"
                 SKIP_REGULAR_EXPRESSION "RESULT: SKIP"
    )
  endif ()
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V milkv-duo: tests registered (${PLATFORM_NAME})")
