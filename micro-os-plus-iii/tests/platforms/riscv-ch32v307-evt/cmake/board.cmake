# The facts of this board, the platform's (they were
# micro-os-plus-iii-riscv/test/boards/ch32v307-evt/board.cmake). The board's code is
# in this platform's folder; its linker scripts are in ../device/linker-scripts/.
# No `qemu` images: the RISC-V tests run emulated as qemu-riscv64 and
# qemu-riscv32.
#
# -----------------------------------------------------------------------------
# board.cmake -- WCH CH32V307V-EVT-R1 (CH32V307VCT6, one QingKe V4F core).
#
# RV32IMAFC, single-precision FPU (misa 0x40901125, measured), ilp32f.
# Code runs from user flash (linker.ld, 256 KiB code + 64 KiB SRAM as configured).
# Clocks 144 MHz from the 8 MHz HSE; console USART1 PA9/PA10 115200 through
# the on-board WCH-LinkE's CDC port; LED1 on PD0 (jumper, active low); key S2
# on PA1 (active low). hw.sh drives the on-board WCH-LinkE with the WCH
# (MounRiver) OpenOCD.
#
# No CLINT and no machine timer/software interrupt: interrupts go through
# the PFIC, and the port runs its PFIC path (RISCV_PFIC). No QEMU image:
# QEMU does not model the CH32V307 or the PFIC.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt")

set (UOS_BOARD_FLAGS
     -march=rv32imafc_zicsr_zifencei -mabi=ilp32f -mcmodel=medany)

set (UOS_BOARD_NCPU 1)

set (UOS_BOARD_CAPS led uart key semihosting)

set (UOS_BOARD_LINKER_HW "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt/device/linker-scripts/linker.ld")

set (UOS_BOARD_LIBS "")

# RISCV_PFIC: the port's QingKe PFIC path (include/pfic.hpp): the software
# interrupt as PendSV, SysTick as the tick, the PFIC threshold as BASEPRI,
# nested interrupts.
# OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES: 64 KiB of SRAM; the port's
# default (8 KiB) let six fp-switch threads exhaust the free store
# (bad_alloc, measured). 2 KiB is the port's minimum stack.
set (UOS_BOARD_DEFINES
     RISCV_PFIC
     OS_INTEGER_RTOS_DEFAULT_STACK_SIZE_BYTES=2048
     SEMIHOST_TRAP_CHOSEN
     "PORT_GREETING=\"µOS++ WCH CH32V307V-EVT (QingKe V4F RV32)\"")

# USB device on the high-speed port P6 (USBHS): the TinyUSB core and CDC
# class the Pico 2 carries (test/boards/pico2/usb/tinyusb), with TinyUSB's
# WCH USBHS driver from the same TinyUSB commit (portable/wch), the bring-up
# (usb/ch32-usb.cpp: clocks, interrupt), the shim for the one WCH header the
# driver includes (usb/wch-shim/ch32v30x.h), and this board's high-speed
# configuration and descriptors (usb/cdc). Linked only into the USB tests
# (test/ch32v307-evt/tests.cmake).
set (_pico2_usb "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb")
set (UOS_BOARD_USB_INCLUDE
     "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt/usb/cdc"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt/usb"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt/usb/wch-shim"
     "${_pico2_usb}/tinyusb")
set (UOS_BOARD_USB_SOURCES
     "${_pico2_usb}/tinyusb/tusb.c"
     "${_pico2_usb}/tinyusb/common/tusb_fifo.c"
     "${_pico2_usb}/tinyusb/device/usbd.c"
     "${_pico2_usb}/tinyusb/class/cdc/cdc_device.c"
     "${_pico2_usb}/tinyusb/portable/wch/dcd_ch32_usbhs.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt/usb/ch32-usb.cpp"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-ch32v307-evt/usb/cdc/usb_descriptors.c")

# The board's id, as the port named it (test/boards/<BOARD>/).
set (BOARD "ch32v307-evt")
