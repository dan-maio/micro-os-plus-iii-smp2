# -----------------------------------------------------------------------------
# Platform: µOS++ III RISC-V on the Milk-V Duo (CV1800B, T-Head C906, RV64GC).
#
# The library under test is the RISC-V port (micro-os-plus-iii-riscv), linked
# into build/<config>/xpacks/@micro-os-plus/ by `xpm run link-deps`. The board
# is this platform's (cmake/board.cmake, include/, src/).
# -----------------------------------------------------------------------------

message (
  VERBOSE
  "Including tests/platforms/${PLATFORM_NAME}/cmake/definitions.cmake..."
)

set (xpack_platform_compile_definition "MICRO_OS_PLUS_PLATFORM_RISCV_MILKV_DUO")
