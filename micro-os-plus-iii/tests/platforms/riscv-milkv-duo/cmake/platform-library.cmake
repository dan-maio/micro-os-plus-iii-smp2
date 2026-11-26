# -----------------------------------------------------------------------------
# Platform library — µOS++ III RISC-V on the Milk-V Duo.
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

add_library (platform-riscv-milkv-duo-interface INTERFACE EXCLUDE_FROM_ALL)

target_include_directories (
  platform-riscv-milkv-duo-interface INTERFACE "include"
)

target_compile_definitions (
  platform-riscv-milkv-duo-interface
  INTERFACE "${xpack_platform_compile_definition}" _GNU_SOURCE
)

target_link_libraries (
  platform-riscv-milkv-duo-interface
  INTERFACE
    micro-os-plus::riscv
    micro-os-plus::board
    micro-os-plus::iii
)

add_library (micro-os-plus::platform ALIAS platform-riscv-milkv-duo-interface)
message (
  VERBOSE
  "> micro-os-plus::platform -> platform-riscv-milkv-duo-interface"
)
