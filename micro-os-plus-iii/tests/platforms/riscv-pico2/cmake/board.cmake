# The facts of this board, the platform's (they were
# micro-os-plus-iii-riscv/test/boards/pico2/board.cmake). The board's code is
# in this platform's folder; its linker scripts are in ../device/linker-scripts/.
# No `qemu` images: the RISC-V tests run emulated as qemu-riscv64 and
# qemu-riscv32.
#
# -----------------------------------------------------------------------------
# board.cmake -- Raspberry Pi Pico 2 (RP2350) on its two Hazard3 cores.
#
# RV32IMAC + Zba/Zbb/Zbs/Zbkb/Zcb/Zcmp, no FPU, compiled as the pico-sdk
# compiles for Hazard3. µOS++ runs SMP on both harts (OS_NCPU=2).
#
# `hwd` images run from flash (linker.ld): the boot ROM sees the RISC-V
# IMAGE_DEF block (src/image-def.S), switches both cores to Hazard3 and
# enters _start on core 0. Clocks 150 MHz, mtime 1 MHz from the TICKS block,
# UART0 on GPIO0/1, LED GPIO25, core 1 launched through the boot ROM FIFO.
# hw.sh drives the Raspberry Pi Debug Probe with the Raspberry Pi OpenOCD.
# `qemu` images run on QEMU virt, RV32, two harts (the RP2350 is not
# modelled).
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2")

set (UOS_BOARD_FLAGS
     -march=rv32ima_zicsr_zifencei_zba_zbb_zbs_zbkb_zca_zcb_zcmp -mabi=ilp32
     -mcmodel=medany)
# The libraries (libgcc, newlib, libstdc++) to link. The toolchain has no
# multilib for the -march above, and the closest it picks by itself is
# rv32ia/ilp32 -- no M: libgcc's software float then multiplies through a
# shift-and-add __mulsi3, four calls per float multiply (measured in
# smp-mat-test). rv32imc/ilp32 has the same ABI and uses mul. Link only: the
# code itself keeps the full Hazard3 -march (A, Zb*, Zc*). Hazard3 has no FPU,
# so float stays in software either way.
set (UOS_BOARD_LINK_FLAGS -march=rv32imc_zicsr_zifencei)

set (UOS_BOARD_NCPU 2)

set (UOS_BOARD_CAPS smp led uart semihosting)

set (UOS_BOARD_LINKER_HW "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/device/linker-scripts/linker.ld")
set (UOS_BOARD_LINKER_QEMU "")

set (UOS_BOARD_LIBS "")

set (UOS_BOARD_DEFINES
     SEMIHOST_TRAP_CHOSEN
     "PORT_GREETING=\"µOS++ Raspberry Pi Pico 2 (RP2350, 2x Hazard3 RV32)\"")

# The USB device stack (TinyUSB, its RP2040/RP2350 device driver and the
# pico-sdk shim it needs), as the Cortex-M pico2 board carries it, made
# RISC-V where it was Arm (barriers, critical sections). Only the USB tests
# link it (test/pico2/tests.cmake), so it is not under src/.
set (UOS_BOARD_USB_INCLUDE
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/pico-shim")
set (UOS_BOARD_USB_CORE_SOURCES
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/tusb.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/common/tusb_fifo.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/device/usbd.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/portable/raspberrypi/rp2040/dcd_rp2040.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/portable/raspberrypi/rp2040/rp2040_usb.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/pico-shim/pico-shim.c")
set (UOS_BOARD_USB_CDC_INCLUDE "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/cdc")
set (UOS_BOARD_USB_CDC_SOURCES
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/class/cdc/cdc_device.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/cdc/usb_descriptors.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/cdc/usb_cdc.c")
set (UOS_BOARD_USB_HID_INCLUDE "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/hid")
set (UOS_BOARD_USB_HID_SOURCES
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/tinyusb/class/hid/hid_device.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/hid/usb_descriptors.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb/hid/usb_hid.c")

# The board's id, as the port named it (test/boards/<BOARD>/).
set (BOARD "pico2")
