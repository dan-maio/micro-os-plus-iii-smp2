# -----------------------------------------------------------------------------
# Folders to add_subdirectory() for this platform.
# -----------------------------------------------------------------------------

message (
  VERBOSE
  "Including tests/platforms/${PLATFORM_NAME}/cmake/dependencies-folders.cmake..."
)

set (
  xpack_dependencies_folders
  # The portable harness suites. Their sources become test::<suite>, which the
  # tests' builder (board-tests/) builds for UOS_TESTS_SUITES.
  "${CMAKE_SOURCE_DIR}/sources/fp-switch"
  "${CMAKE_SOURCE_DIR}/sources/mutex-stress"
  "${CMAKE_SOURCE_DIR}/sources/rtos-apis"
  "${CMAKE_SOURCE_DIR}/sources/cmsis-os-validator"
  # The RISC-V port (xpm run link-deps).
  "${CMAKE_BINARY_DIR}/xpacks/@micro-os-plus/micro-os-plus-iii-riscv"
  # The SOURCE_DIR is the `tests` folder.
  "${CMAKE_SOURCE_DIR}/xpacks/@xpacks/arm-cmsis-rtos-validator"
  "${CMAKE_SOURCE_DIR}/xpacks/@xpacks/chan-fatfs"
)
