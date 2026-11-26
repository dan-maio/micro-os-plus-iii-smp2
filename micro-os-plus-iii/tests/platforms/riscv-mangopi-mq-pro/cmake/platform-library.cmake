# -----------------------------------------------------------------------------
# Platform library — µOS++ III RISC-V on the MangoPi MQ-Pro (Allwinner D1, T-Head C906, RV64GC).
#
# micro-os-plus::platform is the port plus the board and the kernel. The
# board's tests and the harness suites link the port and the board directly
# (through the tests' builder); this target is what a consumer of the whole
# platform would link.
# -----------------------------------------------------------------------------

message (
  VERBOSE
  "Including tests/platforms/${PLATFORM_NAME}/cmake/platform-library.cmake..."
)

# The board: its facts (cmake/board.cmake) and micro-os-plus::board.
include ("${CMAKE_CURRENT_LIST_DIR}/board.cmake")
include ("${CMAKE_SOURCE_DIR}/cmake/uos-riscv-board.cmake")
uos_riscv_board_library ()

add_library (platform-riscv-mangopi-mq-pro-interface INTERFACE EXCLUDE_FROM_ALL)

target_include_directories (
  platform-riscv-mangopi-mq-pro-interface INTERFACE "include"
)

target_compile_definitions (
  platform-riscv-mangopi-mq-pro-interface
  INTERFACE "${xpack_platform_compile_definition}" _GNU_SOURCE
)

target_link_libraries (
  platform-riscv-mangopi-mq-pro-interface
  INTERFACE
    micro-os-plus::riscv
    micro-os-plus::board
    micro-os-plus::iii
)

add_library (micro-os-plus::platform ALIAS platform-riscv-mangopi-mq-pro-interface)
message (
  VERBOSE
  "> micro-os-plus::platform -> platform-riscv-mangopi-mq-pro-interface"
)
