# -----------------------------------------------------------------------------
# Platform library — µOS++ III RISC-V on QEMU's virt machine (RV64).
#
# The board is the port's virt machine (micro-os-plus::riscv-qemu-virt-board: a virt
# CLINT, an NS16550 console on -serial, a virtual LED, the 64-bit
# code-generation flags); its linker script is the port's qemu/virt/linker.ld.
# -----------------------------------------------------------------------------

message (
  VERBOSE
  "Including tests/platforms/${PLATFORM_NAME}/cmake/platform-library.cmake..."
)

# The board facts the tests' builder reads. One hart: every image runs on
# `-smp 1`. Only `qemu` images: there is no hardware behind this platform.
set (UOS_BOARD_SRC_DIR "${UOS_RISCV_DIR}/qemu/virt")
set (UOS_BOARD_NCPU 1)
set (UOS_BOARD_CAPS led uart semihosting)
set (UOS_BOARD_LINKER_HW "")
set (UOS_BOARD_LINKER_QEMU "${UOS_RISCV_DIR}/qemu/virt/linker.ld")
set (BOARD "qemu-virt-rv64")

add_library (board-${PLATFORM_NAME}-interface INTERFACE)
target_link_libraries (board-${PLATFORM_NAME}-interface INTERFACE
                       micro-os-plus::riscv-qemu-virt-board)
add_library (micro-os-plus::board ALIAS board-${PLATFORM_NAME}-interface)

add_library (platform-qemu-riscv64-interface INTERFACE EXCLUDE_FROM_ALL)

target_include_directories (
  platform-qemu-riscv64-interface INTERFACE "include"
)

target_compile_definitions (
  platform-qemu-riscv64-interface
  INTERFACE "${xpack_platform_compile_definition}" _GNU_SOURCE
)

target_link_libraries (
  platform-qemu-riscv64-interface
  INTERFACE
    micro-os-plus::riscv
    micro-os-plus::board
    micro-os-plus::iii
)

add_library (micro-os-plus::platform ALIAS platform-qemu-riscv64-interface)
message (
  VERBOSE
  "> micro-os-plus::platform -> platform-qemu-riscv64-interface"
)
