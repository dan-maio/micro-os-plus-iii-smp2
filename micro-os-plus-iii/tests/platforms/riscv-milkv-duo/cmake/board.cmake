# The facts of this board, the platform's (they were
# micro-os-plus-iii-riscv/test/boards/milkv-duo/board.cmake). The board's code
# is in this platform's folder (include/, src/, usb/); its linker script is in
# ../device/linker-scripts/.
#
# -----------------------------------------------------------------------------
# Milk-V Duo (64 MB) -- Sophgo CV1800B, one T-Head C906 (RV64GC) driven by
# µOS++ in machine mode, 64 MiB DDR2.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_SOURCE_DIR}/platforms/riscv-milkv-duo")

# RV64GC with the double-float ABI: the C906 has the F and D extensions, and
# the port saves the FP register file on every trap. medany: the image lives
# at 0x80050000, outside the +-2 GiB of address 0 that medlow assumes.
set (UOS_BOARD_FLAGS -march=rv64imafdc_zicsr_zifencei -mabi=lp64d
                     -mcmodel=medany -mtune=thead-c906)
set (UOS_BOARD_NCPU  1)

# What this port can drive on this board. The test matrix asks about
# capabilities, never about board names.
set (UOS_BOARD_CAPS  led uart semihosting mmu fpu)

# The hardware image: loaded into DDR at 0x80050000 by OpenOCD.
set (UOS_BOARD_LINKER_HW
     "${CMAKE_SOURCE_DIR}/platforms/riscv-milkv-duo/device/linker-scripts/linker.ld")

# No `qemu` image: QEMU does not model the CV1800B. This board's tests run
# emulated on QEMU's virt machine as the platforms qemu-riscv64 and
# qemu-riscv32, which build them from this folder's tests/.
set (UOS_BOARD_LINKER_QEMU "")

set (UOS_BOARD_LIBS "")

# SEMIHOST_TRAP_CHOSEN: the board contract asks every semihosted board to say
# which trap its debugger expects. RISC-V has exactly one (the slli/ebreak/
# srai sequence, semihosting.hpp), so there is no second define to choose.
set (UOS_BOARD_DEFINES
     SEMIHOST_TRAP_CHOSEN
     "PORT_GREETING=\"µOS++ Milk-V Duo (CV1800B, T-Head C906 RV64GC)\"")

# The USB device stack for the two USB tests: the TinyUSB core and class
# drivers the RISC-V Pico 2 carries (upstream commit 91fbbd192), the upstream
# DWC2 driver of the same commit with this board's platform header
# (usb/dwc2/dwc2_cv180x.h), the bring-up (usb/duo-usb.cpp, BL2's sequence),
# and the portable CDC/HID glue and descriptors of the Pico 2. Linked only
# into the USB tests (tests/tests.cmake).
#
# UNTIL THE RISC-V PICO 2 MOVES HERE: its USB tree is still the port's
# (test/boards/pico2/usb); it becomes platforms/riscv-pico2/usb.
set (_pico2_usb "${UOS_RISCV_DIR}/test/boards/pico2/usb")
set (_duo "${CMAKE_SOURCE_DIR}/platforms/riscv-milkv-duo")
set (UOS_BOARD_USB_INCLUDE
     "${_duo}/usb"
     "${_duo}/usb/dwc2"
     "${_pico2_usb}/tinyusb")
set (UOS_BOARD_USB_CORE_SOURCES
     "${_pico2_usb}/tinyusb/tusb.c"
     "${_pico2_usb}/tinyusb/common/tusb_fifo.c"
     "${_pico2_usb}/tinyusb/device/usbd.c"
     "${_duo}/usb/dwc2/dcd_dwc2.c"
     "${_duo}/usb/dwc2/dwc2_common.c"
     "${_duo}/usb/duo-usb.cpp")
set (UOS_BOARD_USB_CDC_INCLUDE "${_duo}/usb/cdc" "${_pico2_usb}/cdc")
set (UOS_BOARD_USB_CDC_SOURCES
     "${_pico2_usb}/tinyusb/class/cdc/cdc_device.c"
     "${_pico2_usb}/cdc/usb_descriptors.c"
     "${_pico2_usb}/cdc/usb_cdc.c")
set (UOS_BOARD_USB_HID_INCLUDE "${_duo}/usb/hid" "${_pico2_usb}/hid")
set (UOS_BOARD_USB_HID_SOURCES
     "${_pico2_usb}/tinyusb/class/hid/hid_device.c"
     "${_pico2_usb}/hid/usb_descriptors.c"
     "${_pico2_usb}/hid/usb_hid.c")

# The board's id, as the port named it (test/boards/<BOARD>/).
set (BOARD "milkv-duo")
