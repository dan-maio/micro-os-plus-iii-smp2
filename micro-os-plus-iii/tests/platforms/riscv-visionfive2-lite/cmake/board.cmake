# The facts of this board, the platform's (they were
# micro-os-plus-iii-riscv/test/boards/visionfive2-lite/board.cmake). The board's code is
# in this platform's folder; its linker scripts are in ../device/linker-scripts/.
# No `qemu` images: the RISC-V tests run emulated as qemu-riscv64 and
# qemu-riscv32.
#
# -----------------------------------------------------------------------------
# StarFive VisionFive 2 Lite -- StarFive JH-7110S, four SiFive U74 harts
# (RV64GC + Zba/Zbb, Sv39; mhartid 1..4) and one S7 monitor hart (mhartid 0,
# rv64imac, not used). µOS++ runs on the U74 harts in machine mode (SMP on
# all four, or on hart 1 alone), entirely in DDR.
#
# Sources (jh7110.dtsi / jh7110-common.dtsi, Linux):
#   cpu@1..4  riscv,isa "rv64imafdc_zba_zbb", mmu-type "riscv,sv39"
#   timebase-frequency = <4000000>
#
# One file per board, discovered by the presence of this file.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite")

# medany: the image lives at 0x40200000, outside medlow's +-2 GiB of 0 range
# for the MMIO it addresses as constants (harmless either way, as on the Duo).
# Zba/Zbb change nothing in the multilib selection (the same
# rv64imafdc_zicsr_zaamo_zalrsc/lp64d as the Duo; -print-multi-directory).
set (UOS_BOARD_FLAGS -march=rv64imafdc_zicsr_zifencei_zba_zbb -mabi=lp64d
                     -mcmodel=medany -mtune=sifive-7-series)
# The four U74 harts (mhartid 1..4) are µOS++ CPUs 0..3: RISCV_HART_BASE=1
# below (include/port_ctx.hpp). Which tests run SMP is test/visionfive2-lite/
# tests.cmake's choice; the others are built for one CPU.
set (UOS_BOARD_NCPU  4)

set (UOS_BOARD_CAPS  led uart semihosting mmu fpu)

# The hardware image: loaded into DDR at 0x40200000 by OpenOCD (hw.sh), with
# the board stopped at the U-Boot prompt (DDR trained, UART0 configured).
set (UOS_BOARD_LINKER_HW "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/device/linker-scripts/linker.ld")

# No QEMU image: hardware only (UOS_BOARD_LINKER_QEMU unset).

set (UOS_BOARD_LIBS "")

set (UOS_BOARD_DEFINES
     SEMIHOST_TRAP_CHOSEN
     RISCV_HART_BASE=1
     "PORT_GREETING=\"µOS++ StarFive VisionFive 2 Lite (JH-7110S, SiFive U74 RV64GC)\"")

# The USB device stack for the USB test: the TinyUSB core and class drivers
# the Pico 2 carries (test/boards/pico2/usb/tinyusb), the Cadence USBSS-DRD
# device driver written for this board (usb/cdns3/dcd_cdns3.c, from Linux's
# cdns3 gadget driver), the bring-up and DMA cache maintenance
# (usb/vf2-usb.cpp), and the portable CDC glue and descriptors of the
# Pico 2. Linked only into the USB test (test/visionfive2-lite/tests.cmake).
set (_pico2_usb "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2/usb")
set (UOS_BOARD_USB_INCLUDE
     "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/usb"
     "${_pico2_usb}/tinyusb")
set (UOS_BOARD_USB_CORE_SOURCES
     "${_pico2_usb}/tinyusb/tusb.c"
     "${_pico2_usb}/tinyusb/common/tusb_fifo.c"
     "${_pico2_usb}/tinyusb/device/usbd.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/usb/cdns3/dcd_cdns3.c"
     "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/usb/vf2-usb.cpp")
set (UOS_BOARD_USB_CDC_INCLUDE "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/usb/cdc" "${_pico2_usb}/cdc")
set (UOS_BOARD_USB_CDC_SOURCES
     "${_pico2_usb}/tinyusb/class/cdc/cdc_device.c"
     "${_pico2_usb}/cdc/usb_descriptors.c"
     "${_pico2_usb}/cdc/usb_cdc.c")
# The binary CDC file test (smp-test-usb-cdc-file) drives TinyUSB's CDC API
# directly: the descriptors, not the line-buffered glue (usb_cdc.c), which
# echoes and prints status text the host could reflect.
set (UOS_BOARD_USB_CDC_RAW_SOURCES
     "${_pico2_usb}/tinyusb/class/cdc/cdc_device.c"
     "${_pico2_usb}/cdc/usb_descriptors.c")

# The board's id, as the port named it (test/boards/<BOARD>/).
set (BOARD "visionfive2-lite")
