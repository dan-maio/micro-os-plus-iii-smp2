# -----------------------------------------------------------------------------
# Platform: µOS++ III RISC-V on QEMU's virt machine, RV64GC (with the F and D extensions).
#
# The library under test is the RISC-V port (micro-os-plus-iii-riscv), linked
# into build/<config>/xpacks/@micro-os-plus/ by `xpm run link-deps`. The board
# is the port's own generic target, QEMU's virt machine (qemu/virt/).
# -----------------------------------------------------------------------------

message (
  VERBOSE
  "Including tests/platforms/${PLATFORM_NAME}/cmake/definitions.cmake..."
)

set (xpack_platform_compile_definition "MICRO_OS_PLUS_PLATFORM_QEMU_RISCV64")
