# The facts of this board, the platform's (they were
# micro-os-plus-iii-riscv/test/boards/mangopi-mq-pro/board.cmake). The board's code is
# in this platform's folder; its linker scripts are in ../device/linker-scripts/.
# No `qemu` images: the RISC-V tests run emulated as qemu-riscv64 and
# qemu-riscv32.
#
# -----------------------------------------------------------------------------
# MangoPi MQ-Pro -- Allwinner D1, one T-Head C906 (RV64GC), 512 MiB/1 GiB DDR3.
#
# The board is driven entirely from DRAM: xfel initializes DDR, writes the
# flat image at 0x40000000 and jumps to it.  There is no JTAG/semihosting on
# this setup; the test verdict comes back over UART0 (PB8/PB9, 115200) and the
# test resets itself back into FEL with the D1 watchdog + Allwinner efex flag,
# so hw.sh can load the next image without touching the board.
#
# One file per board, discovered by the presence of this file.
# -----------------------------------------------------------------------------

set (UOS_BOARD_SRC_DIR "${CMAKE_SOURCE_DIR}/platforms/riscv-mangopi-mq-pro")

# RV64GC with the double-float ABI: the C906 has F and D, and the port saves
# the FP register file on every trap. medany: the image lives at 0x40000000.
set (UOS_BOARD_FLAGS -march=rv64imafdc_zicsr_zifencei -mabi=lp64d
                     -mcmodel=medany -mtune=thead-c906)
set (UOS_BOARD_NCPU  1)

# Capabilities the tests may ask for.  `semihosting` is true for the QEMU
# image; on hardware there is no debugger, and hw_result.hpp (test/<board>/
# include) ends the run over UART + reset instead.  The semihosting-test
# reports SKIP on hardware.
set (UOS_BOARD_CAPS  led uart semihosting mmu fpu)

# The hardware image: xfel writes it at 0x40000000 and executes it.
set (UOS_BOARD_LINKER_HW "${CMAKE_SOURCE_DIR}/platforms/riscv-mangopi-mq-pro/device/linker-scripts/linker.ld")

# QEMU does not model the D1; the `qemu` image of each test runs on QEMU's
# virt machine instead, exactly as the Milk-V Duo does.
set (UOS_BOARD_LINKER_QEMU "")

set (UOS_BOARD_LIBS "")

set (UOS_BOARD_DEFINES
     SEMIHOST_TRAP_CHOSEN
     "PORT_GREETING=\"µOS++ MangoPi MQ-Pro (Allwinner D1, T-Head C906 RV64GC)\"")

# The board's id, as the port named it (test/boards/<BOARD>/).
set (BOARD "mangopi-mq-pro")
