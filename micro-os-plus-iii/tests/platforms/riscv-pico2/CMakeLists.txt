# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the Raspberry Pi Pico 2 (RP2350, 2x Hazard3, RV32IMAC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# hwd tests need the board: they carry the `hwd` label, which the
# configuration's `test` action skips (`ctest -LE hwd`), and run one at a time
# (RUN_SERIAL); OpenOCD programs (or, for a `-ram` test, loads) the image and
# resets the board for each, and the test's semihosting SYS_EXIT ends it. The
# UART is not opened. The USB device tests need the PC's side
# (platforms/riscv-pico2/usb-host.py cdc|hid), run by hand.
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

set (UOS_TESTS_PORT riscv)
# Upstream's suites (tests/sources/<suite>/), on the common end of a portable
# test (board-tests/portable-test-end.cpp). The suites run on one hart (the core-1 launch is a board test's act); the `-ram` variants are board tests (tests/tests.cmake).
set (UOS_TESTS_SUITES fp-switch mutex-stress rtos-apis cmsis-os-validator)
# The board-only tests: this platform's tests/ (tests.cmake and one folder per
# test).
set (UOS_TESTS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

# -----------------------------------------------------------------------------
# The hardware run.
include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

# The Raspberry Pi OpenOCD (UOS_RISCV_PICO_OPENOCD, _rpi_ocd_args).
include ("${CMAKE_CURRENT_SOURCE_DIR}/cmake/rpi-openocd.cmake")

# The two sessions (the port's former test/boards/pico2/hw.sh). From flash:
# program, then boot it. From SRAM (`-ram`, linker-ram.ld, entry 0x20000000):
# load and verify over the debugger, with verify_image's work area in the
# image's NOLOAD heap, never over the loaded sections; nothing goes to flash.
# Core 1 waits in the boot ROM until the test launches it; semihosting is on
# for both.
set (_hwd_flash
     "init" "reset init" "program {@ELF@} verify" "reset init"
     "targets rp2350.rv1" "arm semihosting enable"
     "targets rp2350.rv0" "arm semihosting enable" "resume")
set (_hwd_ram
     "init" "reset init"
     "rp2350.rv0 configure -work-area-phys 0x20078000 -work-area-size 0x8000"
     "load_image {@ELF@}" "verify_image {@ELF@}"
     "targets rp2350.rv1" "arm semihosting enable"
     "targets rp2350.rv0" "arm semihosting enable"
     "reg pc 0x20000000" "resume")

foreach (_app IN LISTS UOS_TESTS_APPS)
  if (_app MATCHES "-ram$")
    set (_hwd_commands ${_hwd_ram})
  else ()
    set (_hwd_commands ${_hwd_flash})
  endif ()
  uos_tests_add_openocd (
    NAME "${PLATFORM_NAME}-${_app}-hwd" APP "${_app}"
    ELF "$<TARGET_FILE:${_app}-hwd>" CFG "${CMAKE_CURRENT_SOURCE_DIR}/openocd.cfg"
    ${_rpi_ocd_args}
    COMMANDS ${_hwd_commands} TIMEOUT 300
  )
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V pico2: hwd tests registered (${PLATFORM_NAME})")
