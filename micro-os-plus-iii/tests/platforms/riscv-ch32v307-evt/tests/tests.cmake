# -----------------------------------------------------------------------------
# What the CH32V307V-EVT's tests need beyond the defaults.
#
# The kernel runs on the port's PFIC path (board.cmake RISCV_PFIC). Images
# run from flash (board linker.ld); there is no QEMU image.
#
#   board-basics   kernel-free (micro-os-plus::riscv-bare): start-up,
#                  clocks, console, LED, key, semihosting
#   smp-test1      the kernel, single core (the Pico 2's bring-up test)
#   pfic-test      the PFIC path itself: nesting, threshold, preemption
#   usb-cdc-stream a file round trip over USB HS (port P6), streamed; the PC
#                  side is usb-cdc-stream/usb-stream-host.py
#   the harness suites (below)
# -----------------------------------------------------------------------------

set (BOARD_TEST_NO_KERNEL board-basics)

# Upstream's suites are UOS_TESTS_SUITES (../CMakeLists.txt), from
# tests/sources/<suite>.

# The USB tests: TinyUSB with the WCH USBHS driver (board.cmake
# UOS_BOARD_USB_*). Their tusb_config.h directory comes first.
set (_ch32_usb_tests usb-cdc-stream)

function (board_test_sources _app _out)
  if ("${_app}" IN_LIST _ch32_usb_tests)
    set (${_out} ${UOS_BOARD_USB_SOURCES} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_includes _app _out)
  if ("${_app}" IN_LIST _ch32_usb_tests)
    set (${_out} ${UOS_BOARD_USB_INCLUDE} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
