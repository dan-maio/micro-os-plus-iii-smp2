# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the WCH CH32V307V-EVT (QingKe V4F, RV32IMAFC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# hwd tests need the board: label `hwd` (the configuration's `test` skips
# them), one at a time. A fixture programs the flash and compares it with the
# image (cmake/program.cmake); the test session then runs it from reset with
# semihosting, and the test's SYS_EXIT ends it: OpenOCD's exit status, the
# test's exit code, is the verdict (the RESULT line goes to the UART only).
# board-basics waits for key S2: label `hwd-manual`. The UART is not opened.
# The USB test needs the PC's side, run by hand.
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

set (UOS_TESTS_PORT riscv)
# Upstream's suites (tests/sources/<suite>/), on the common end of a portable
# test (board-tests/portable-test-end.cpp). The board runs one hart, on the port's PFIC path (RISCV_PFIC).
set (UOS_TESTS_SUITES blinky fp-switch mutex-stress instrumentation rtos-apis cmsis-os-validator)
# The board-only tests: this platform's tests/ (tests.cmake and one folder per
# test).
set (UOS_TESTS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

# -----------------------------------------------------------------------------
# The hardware run (the port's former test/boards/ch32v307-evt/hw.sh).
include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

# The toolchain's binutils, by full path: CTest runs the tests outside xpm,
# without the configuration's xpacks/.bin on PATH.
get_filename_component (_tool_bin "${CMAKE_C_COMPILER}" DIRECTORY)
set (_readelf "${_tool_bin}/riscv-none-elf-readelf")
set (_objcopy "${_tool_bin}/riscv-none-elf-objcopy")

# The WCH OpenOCD (openocd-wch/build-openocd-wch.sh installs it here).
set (UOS_RISCV_WCH_OPENOCD "$ENV{HOME}/.local/openocd-wch/bin/openocd" CACHE FILEPATH
     "The WCH OpenOCD (the MounRiver fork); the WCH-LinkE needs it")
get_filename_component (_wch_dir "${UOS_RISCV_WCH_OPENOCD}" DIRECTORY)
get_filename_component (_wch_dir "${_wch_dir}" DIRECTORY)
set (UOS_RISCV_WCH_OPENOCD_SCRIPTS "${_wch_dir}/share/openocd/scripts" CACHE PATH
     "The WCH OpenOCD's scripts (interface/wlink.cfg, target/wch-riscv.cfg)")
if (NOT EXISTS "${UOS_RISCV_WCH_OPENOCD}")
  message (WARNING "${PLATFORM_NAME}: no WCH OpenOCD at ${UOS_RISCV_WCH_OPENOCD}; "
           "the hwd tests cannot run. Build it (openocd-wch/build-openocd-wch.sh) "
           "or set UOS_RISCV_WCH_OPENOCD, and configure again.")
endif ()

foreach (_app IN LISTS UOS_TESTS_APPS)
  set (_name "${PLATFORM_NAME}-${_app}-hwd")
  uos_tests_add_openocd (
    NAME "${_name}" APP "${_app}"
    ELF "$<TARGET_FILE:${_app}-hwd>" CFG "${CMAKE_CURRENT_SOURCE_DIR}/openocd.cfg"
    OPENOCD "${UOS_RISCV_WCH_OPENOCD}" SCRIPTS "${UOS_RISCV_WCH_OPENOCD_SCRIPTS}"
    COMMANDS "init" "reset halt" "arm semihosting enable" "resume"
    # The board's console is its UART only: the verdict is the semihosting
    # exit code (OpenOCD's exit status), as the port's former hw.sh read it.
    VERDICT_EXIT_CODE
    TIMEOUT 300
  )
  add_test (NAME "${_name}-program"
    COMMAND "${CMAKE_COMMAND}"
            "-DOPENOCD=${UOS_RISCV_WCH_OPENOCD}"
            "-DSCRIPTS=${UOS_RISCV_WCH_OPENOCD_SCRIPTS}"
            "-DREADELF=${_readelf}" "-DELF=$<TARGET_FILE:${_app}-hwd>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/${_name}-program"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/program.cmake")
  set_tests_properties ("${_name}-program" PROPERTIES
    LABELS "hwd-reset" TIMEOUT 240 RUN_SERIAL TRUE
    FIXTURES_SETUP "${_name}-program")
  set_tests_properties ("${_name}" PROPERTIES
    FIXTURES_REQUIRED "${_name}-program")
  # board-basics waits 10 s for key S2: run it by name, with a hand on the
  # board (as the Arm boards' tests that need a person).
  if (_app STREQUAL "board-basics")
    set_tests_properties ("${_name}" PROPERTIES LABELS "hwd-manual")
  endif ()
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V ch32v307-evt: hwd tests registered (${PLATFORM_NAME})")
