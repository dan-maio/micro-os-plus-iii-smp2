# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on the StarFive VisionFive 2 Lite (JH-7110S, 4x SiFive U74, RV64GC).
#
# The board's tests are this platform's tests/<app>/, plus upstream's suites
# (tests/sources/<suite>/); the tests' builder (board-tests/) compiles each as
# `-hwd`, for the board.
#
# hwd tests need the board at the U-Boot prompt: label `hwd` (the
# configuration's `test` skips them), one at a time. OpenOCD loads the image
# into DDR and starts the harts it drives (vf2-hwd.tcl); the test's
# semihosting SYS_EXIT ends it. The UART is not opened. The probe:
# UOS_VF2_PROBE. The tests that need the PC's side start it themselves
# (cmake/uos-hwd-host.py): the USB device tests (CDC-ACM, the file round
# trips; the USB-C port connected to this PC) and the two RAM-disk tests
# (Ethernet / Wi-Fi: the PC on the same LAN). The Wi-Fi tests need an access
# point (WIFI_TEST_SSID, WIFI_TEST_PASS on the cmake line).
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

set (UOS_TESTS_PORT riscv)
# Upstream's suites (tests/sources/<suite>/), on the common end of a portable
# test (board-tests/portable-test-end.cpp). The suites run on the four harts: the platform's start-up (src/platform-support.cpp) starts the other three first.
set (UOS_TESTS_SUITES blinky fp-switch mutex-stress instrumentation rtos-apis cmsis-os-validator)
# The board-only tests: this platform's tests/ (tests.cmake and one folder per
# test).
set (UOS_TESTS_BOARD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

# -----------------------------------------------------------------------------
# The hardware run.
include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

# The probe: the DAPLink (0d28:0204) or the Raspberry Pi "JTAG probe on Pico"
# (2e8a:000c, chosen by UOS_HW_ADAPTER_SERIAL when the Pico 2's Debug Probe,
# the same VID:PID, is plugged in too).
set (UOS_VF2_PROBE "daplink" CACHE STRING "The VisionFive 2's JTAG probe: daplink or picoprobe")
set_property (CACHE UOS_VF2_PROBE PROPERTY STRINGS daplink picoprobe)
if (NOT UOS_VF2_PROBE MATCHES "^(daplink|picoprobe)$")
  message (FATAL_ERROR "UOS_VF2_PROBE must be daplink or picoprobe, not '${UOS_VF2_PROBE}'.")
endif ()

# The USB device tests' PC side, started by the run itself
# (cmake/uos-hwd-host.py) once the test's device has enumerated. The file
# round trips send a copy of their name.dat kept in the build folder, where
# the host writes name.dat.back.
find_package (Python3 COMPONENTS Interpreter REQUIRED)
set (_usb_host "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb-host.py")
foreach (_t smp-test-usb-cdc-file smp-test-usb-cdc-file-fs)
  file (COPY "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_t}/name.dat"
        DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/${_t}-host")
endforeach ()

foreach (_app IN LISTS UOS_TESTS_APPS)
  set (_host "")
  if (_app STREQUAL "smp-test-usb-cdc-acm")
    set (_host HOST_VIDPID cafe:4013
         HOST_COMMAND "${Python3_EXECUTABLE}" -I "${_usb_host}" cdc 300)
  elseif (_app STREQUAL "smp-test-usb-cdc-file"
          OR _app STREQUAL "smp-test-usb-cdc-file-fs")
    set (_host HOST_VIDPID cafe:4013 HOST_CWD "${CMAKE_CURRENT_BINARY_DIR}/${_app}-host"
         HOST_COMMAND "${Python3_EXECUTABLE}" -I
                      "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_app}/usb-file-host.py"
                      name.dat 300)
  elseif (_app STREQUAL "smp-net-ram-disk" OR _app STREQUAL "vf2-wifi-ram-disk")
    # The RAM-disk tests' PC side finds the board on the LAN by its beacon:
    # started with the session (no USB device to wait for); a 2 MiB random
    # file, written in the build folder.
    file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${_app}-host")
    set (_host HOST_CWD "${CMAKE_CURRENT_BINARY_DIR}/${_app}-host"
         HOST_COMMAND "${Python3_EXECUTABLE}" -I
                      "${CMAKE_CURRENT_SOURCE_DIR}/tests/${_app}/${_app}-host.py"
                      --no-load --random 2097152)
  endif ()
  # What the session needs to know about the image (vf2-hwd.tcl), written
  # again whenever the image changes (built with it).
  set (_facts "${CMAKE_CURRENT_BINARY_DIR}/facts/${_app}-hwd.facts.tcl")
  add_custom_command (
    OUTPUT "${_facts}"
    COMMAND "${CMAKE_COMMAND}"
            "-DREADELF=${CMAKE_READELF}" "-DNM=${CMAKE_NM}"
            "-DELF=$<TARGET_FILE:${_app}-hwd>" "-DOUT=${_facts}"
            "-DSYMBOLS=riscv_board_park,riscv_secondary_boot"
            -P "${CMAKE_SOURCE_DIR}/cmake/uos-elf-facts.cmake"
    DEPENDS "${_app}-hwd" "${CMAKE_SOURCE_DIR}/cmake/uos-elf-facts.cmake"
    VERBATIM
  )
  add_custom_target ("${_app}-hwd-facts" ALL DEPENDS "${_facts}")
  uos_tests_add_openocd (
    NAME "${PLATFORM_NAME}-${_app}-hwd" APP "${_app}"
    ELF "$<TARGET_FILE:${_app}-hwd>"
    CFG "${CMAKE_CURRENT_SOURCE_DIR}/jh7110_${UOS_VF2_PROBE}.cfg"
    COMMANDS "init" "source {${_facts}}"
             "source {${CMAKE_CURRENT_SOURCE_DIR}/vf2-hwd.tcl}"
             "vf2_hwd_run {@ELF@}"
    RETRY_TRIES 3
    RETRY_IF "Examination failed|dtmcontrol is 0|Target not examined|load_image.*failed|Failed to write memory"
    RETRY_UNLESS "Examined RISC-V core"
    ${_host}
    TIMEOUT 600
  )
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V visionfive2-lite: hwd tests registered (${PLATFORM_NAME})")
