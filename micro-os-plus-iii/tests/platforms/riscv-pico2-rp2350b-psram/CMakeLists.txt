# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the WeAct RP2350B (16 MB flash + 8 MB PSRAM, 2x Hazard3).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# hwd tests need the board: label `hwd` (the configuration's `test` skips
# them), one at a time; OpenOCD programs the image into flash and boots it,
# and the test's semihosting SYS_EXIT ends it. Before each, a fixture makes
# sure the chip runs its RISC-V cores (cmake/arch-switch.cmake). The UART is
# not opened. The two overclocked clock tests wait for the potentiometer and
# 'y' on the UART: label `hwd-manual`, run by name.
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
# The hardware run.
include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

# The Raspberry Pi OpenOCD (UOS_RISCV_PICO_OPENOCD, _rpi_ocd_args).
include ("${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/cmake/rpi-openocd.cmake")

# The session (the port's former test/boards/pico2-rp2350b-psram/hw.sh):
# program into flash and boot it; core 1 waits in the boot ROM until the test
# launches it; semihosting on both cores.
set (_hwd_flash
     "source [find target/rp2350-riscv.cfg]"
     "init" "reset init" "program {@ELF@} verify" "reset init"
     "targets rp2350.rv1" "arm semihosting enable"
     "targets rp2350.rv0" "arm semihosting enable" "resume")

foreach (_app IN LISTS UOS_TESTS_APPS)
  set (_name "${PLATFORM_NAME}-${_app}-hwd")
  uos_tests_add_openocd (
    NAME "${_name}" APP "${_app}"
    ELF "$<TARGET_FILE:${_app}-hwd>" CFG "${CMAKE_CURRENT_SOURCE_DIR}/openocd.cfg"
    ${_rpi_ocd_args} COMMANDS ${_hwd_flash} TIMEOUT 300
  )
  if (_rpi_ocd_args)
    add_test (NAME "${_name}-arch-switch"
      COMMAND "${CMAKE_COMMAND}"
              "-DOPENOCD=${UOS_RISCV_PICO_OPENOCD}" "-DSCRIPTS=${_rpi_ocd_dir}/scripts"
              "-DCFG=${CMAKE_CURRENT_SOURCE_DIR}/openocd.cfg"
              "-DELF=$<TARGET_FILE:${_app}-hwd>"
              "-DLOG=${CMAKE_CURRENT_BINARY_DIR}/${_name}-arch-switch.log"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arch-switch.cmake")
    set_tests_properties ("${_name}-arch-switch" PROPERTIES
      LABELS "hwd-reset" TIMEOUT 200 RUN_SERIAL TRUE
      FIXTURES_SETUP "${_name}-arch-switch")
    set_tests_properties ("${_name}" PROPERTIES
      FIXTURES_REQUIRED "${_name}-arch-switch")
  endif ()
  if (_app STREQUAL "smp-test-nested-clock_200"
      OR _app STREQUAL "smp-test-nested-clock_250")
    set_tests_properties ("${_name}" PROPERTIES LABELS "hwd-manual")
  endif ()
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V pico2-rp2350b-psram: hwd tests registered (${PLATFORM_NAME})")
