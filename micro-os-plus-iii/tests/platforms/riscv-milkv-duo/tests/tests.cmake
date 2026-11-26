# -----------------------------------------------------------------------------
# What the RISC-V board tests need beyond the defaults.
#
# Every test here is a board test: it has its own os_main(), links
# micro-os-plus::riscv and the board, and ends with a RESULT line and a
# semihosting exit. Upstream's suites (UOS_TESTS_SUITES, in the platforms'
# CMakeLists.txt) are not here: the tests' builder takes them from
# tests/sources/<suite>/.
#
# Three platforms build this folder: riscv-milkv-duo (the board, `hwd`
# images), and qemu-riscv64 and qemu-riscv32 (QEMU's virt machine as the
# board, `qemu` images). The tests use the board interface both have
# (riscv-board.hpp, uart.hpp, led.hpp); the tests below need the Duo's own
# silicon and get no `qemu` image.
# -----------------------------------------------------------------------------

# Hardware only: smp-test-nested drives the CV1800B's PLIC, timers and UART4,
# which QEMU's virt machine does not have.
set (BOARD_TEST_HWD_ONLY smp-test-nested smp-test-usb-cdc-acm smp-test-usb-hid)

# The two USB device tests: TinyUSB (DWC2, slave mode) with their class driver
# and their tusb_config.h -- whose directory goes first on the include path.
set (_usb_cdc_tests smp-test-usb-cdc-acm)
set (_usb_hid_tests smp-test-usb-hid)

function (board_test_sources _app _out)
  if ("${_app}" IN_LIST _usb_cdc_tests)
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
    set (${_out} ${UOS_BOARD_USB_CDC_INCLUDE} ${UOS_BOARD_USB_INCLUDE}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _usb_hid_tests)
    set (${_out} ${UOS_BOARD_USB_HID_INCLUDE} ${UOS_BOARD_USB_INCLUDE}
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
