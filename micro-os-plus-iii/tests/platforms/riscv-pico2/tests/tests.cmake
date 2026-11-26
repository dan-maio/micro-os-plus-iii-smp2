# -----------------------------------------------------------------------------
# tests.cmake -- the Pico 2 (RISC-V) test set.
#
# The board's tests and the `-ram` variants of upstream's suites (folders
# that hold only their name; tests/sources/<suite> is their code). Only `hwd`
# images: the RISC-V tests run emulated as qemu-riscv64 and qemu-riscv32.
# -----------------------------------------------------------------------------

set (_pico2_smp_tests smp-test2 smp-test3 smp-test4 smp-test5 smp-test-ko smp-mat-test smp-test-nested
     smp-mat-test-ram smp-test-usb-cdc-acm smp-test-usb-hid)

# Tests that program RP2350 peripherals directly (TIMER0, SIO): hardware
# only, as on the Cortex-M Pico 2 board.
set (BOARD_TEST_HWD_ONLY smp-test0 smp-test3 smp-test4 smp-test5 smp-test-ko smp-mat-test smp-test-nested
     smp-mat-test-ram cmsis-os-validator-ram mutex-stress-ram rtos-apis-ram
     smp-test-usb-cdc-acm smp-test-usb-hid)

# The two USB device tests link TinyUSB with their class driver and its
# tusb_config.h. A plain Pico 2 has no WS2812 (the Cortex-M CDC test was
# brought up on an RP2350B-MINI-A, RGB LED on GPIO20): PICO2_HAS_WS2812 stays
# undefined, and ws2812.hpp's inline stubs stand in.
set (_usb_cdc_tests smp-test-usb-cdc-acm)
set (_usb_hid_tests smp-test-usb-hid)

function (board_test_sources _app _out)
  if (_app STREQUAL "cmsis-os-validator-ram")
    # The validator's board glue, as the suite itself gets it
    # (src/cmsis-os-validator-support.cpp).
    set (${_out}
         "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/src/cmsis-os-validator-support.cpp"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _usb_cdc_tests)
    set (${_out} ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    set (${_out} ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_HID_SOURCES}
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_includes _app _out)
  if ("${_app}" IN_LIST _usb_cdc_tests)
    set (${_out} "${UOS_BOARD_USB_CDC_INCLUDE}" ${UOS_BOARD_USB_INCLUDE}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    set (${_out} "${UOS_BOARD_USB_HID_INCLUDE}" ${UOS_BOARD_USB_INCLUDE}
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

# `<name>-ram`: the same program run from SRAM, loaded over the debugger
# (linker-ram.ld; hw.sh switches to a RAM session when the image's entry is
# in SRAM). Nothing is written to flash.
function (board_test_linker _app _out)
  if ("${_app}" MATCHES "-ram$")
    set (${_out} "${UOS_BOARD_SRC_DIR}/device/linker-scripts/linker-ram.ld"
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

# Bare-metal probes: no kernel (micro-os-plus::riscv-bare), their own main().
set (BOARD_TEST_NO_KERNEL smp-test0)

function (board_test_ncpu _app _out)
  if ("${_app}" IN_LIST _pico2_smp_tests)
    set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
  else ()
    set (${_out} 1 PARENT_SCOPE)
  endif ()
endfunction ()

# The `-ram` variants of upstream's suites: the same suites run from SRAM
# (their linker script above), on the same start-up and suite settings as the
# suites themselves (UOS_TESTS_SUITES in ../CMakeLists.txt).
function (board_test_libs _app _out)
  set (_suite_libs ${PLATFORM_NAME}-test-startup ${PLATFORM_NAME}-test-suite)
  if (_app STREQUAL "rtos-apis-ram")
    set (${_out} test::rtos-apis xpacks::chan-fatfs micro-os-plus::iii-posix-io
         ${_suite_libs} PARENT_SCOPE)
  elseif (_app STREQUAL "mutex-stress-ram")
    set (${_out} test::mutex-stress ${_suite_libs} PARENT_SCOPE)
  elseif (_app STREQUAL "cmsis-os-validator-ram")
    set (${_out} test::cmsis-os-validator xpacks::arm-cmsis-os-validator
         ${_suite_libs} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
