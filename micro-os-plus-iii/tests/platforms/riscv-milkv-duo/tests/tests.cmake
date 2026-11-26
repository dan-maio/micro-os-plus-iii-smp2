# -----------------------------------------------------------------------------
# What the Milk-V Duo's tests need beyond the defaults.
#
# Every test here is a board test: it has its own os_main(), links
# micro-os-plus::riscv and the board, and ends with a RESULT line and a
# semihosting exit. Upstream's suites (UOS_TESTS_SUITES, in ../CMakeLists.txt)
# are not here: the tests' builder takes them from tests/sources/<suite>/.
#
# The `qemu` image. QEMU does not model the CV1800B, so the emulated image of
# each test links the port against QEMU's virt machine instead of this board
# (micro-os-plus::riscv-qemu-virt: the same port core, a virt CLINT, an
# NS16550 console, a virtual LED, its own linker script). The test's sources
# are unchanged; only what runs under them differs -- exactly how the
# Cortex-M port's pico2 runs its tests on QEMU.
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

function (board_test_qemu_libs _app _out)
  set (${_out} micro-os-plus::riscv-qemu-virt PARENT_SCOPE)
endfunction ()
