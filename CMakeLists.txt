# -----------------------------------------------------------------------------
# micro-os-plus-iii-devices-storage -- the chip-independent storage drivers of
# µOS++ III: the flatfs file system, the FatFs disk glue and the SD card
# interface (sd.hpp, which selects the controller back-end a device package
# provides with SD_BACKEND_*).
#
# Used by the device packages (micro-os-plus-iii-devices-bcm283x, -rk3506);
# it reads no board and no chip. The back-end headers (sd_sdhost.hpp,
# sd_dwmmc.hpp, ...) come from the device package that links this one.
#
# Exports: micro-os-plus::devices-storage
# -----------------------------------------------------------------------------
cmake_minimum_required (VERSION 3.20)

project (micro-os-plus-iii-devices-storage LANGUAGES C CXX)

set (UOS_DEVICES_STORAGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE INTERNAL "")

if (NOT TARGET micro-os-plus::devices-storage)
  add_library (micro-os-plus-iii-devices-storage-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-storage-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/fatfs"
  )
  target_sources (
    micro-os-plus-iii-devices-storage-interface
    INTERFACE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/flatfs.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/fatfs/ff.c"
      "${CMAKE_CURRENT_SOURCE_DIR}/fatfs/diskio_sd.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/fatfs/fatfs_hw.cpp"
  )
  add_library (micro-os-plus::devices-storage
               ALIAS micro-os-plus-iii-devices-storage-interface)
endif ()
