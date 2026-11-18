# -----------------------------------------------------------------------------
# micro-os-plus-iii-devices-rk3506 -- the Rockchip RK3506 devices of µOS++ III
# (Luckfox Lyra B): the SoC headers and the DesignWare SD host (DW-MMC).
#
# The chip-independent part (flatfs, the FatFs glue, sd.hpp) is
# micro-os-plus-iii-devices-storage; sd.hpp selects this back-end with
# SD_BACKEND_DWMMC.
#
# Exports: micro-os-plus::devices-rk3506 (SD over DW-MMC, no USB gadget),
# micro-os-plus::devices-rk3506-sd-posix (the same SD host as a posix-io block
# device, no flatfs/FatFs)
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-devices-rk3506 LANGUAGES C CXX)

set (UOS_DEVICES_RK3506_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

if (NOT TARGET micro-os-plus::devices-rk3506)
  add_library (micro-os-plus-iii-devices-rk3506-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-rk3506-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/rk3506/include"
  )
  target_compile_definitions (
    micro-os-plus-iii-devices-rk3506-interface INTERFACE SD_BACKEND_DWMMC
  )
  target_sources (
    micro-os-plus-iii-devices-rk3506-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rk3506/src/sdmmc.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rk3506/src/sd_dwmmc.cpp"
  )
  target_link_libraries (
    micro-os-plus-iii-devices-rk3506-interface
    INTERFACE micro-os-plus::devices-storage
  )
  add_library (micro-os-plus::devices-rk3506
               ALIAS micro-os-plus-iii-devices-rk3506-interface)
endif ()

# The same SD host as a µOS++ POSIX I/O block device (sd::block_device,
# micro-os-plus-iii-devices-storage), with none of the old file systems: the
# file system comes from @xpacks/chan-fatfs.
if (NOT TARGET micro-os-plus::devices-rk3506-sd-posix)
  add_library (micro-os-plus-iii-devices-rk3506-sd-posix-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-rk3506-sd-posix-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/soc/rk3506/include"
  )
  target_compile_definitions (
    micro-os-plus-iii-devices-rk3506-sd-posix-interface INTERFACE SD_BACKEND_DWMMC
  )
  target_sources (
    micro-os-plus-iii-devices-rk3506-sd-posix-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rk3506/src/sdmmc.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/soc/rk3506/src/sd_dwmmc.cpp"
  )
  target_link_libraries (
    micro-os-plus-iii-devices-rk3506-sd-posix-interface
    INTERFACE micro-os-plus::devices-storage-sd micro-os-plus::devices-storage-posix
  )
  add_library (micro-os-plus::devices-rk3506-sd-posix
               ALIAS micro-os-plus-iii-devices-rk3506-sd-posix-interface)
endif ()
