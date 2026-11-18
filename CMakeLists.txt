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
# Exports: micro-os-plus::devices-storage (flatfs, FatFs R0.15a and its glue,
# sd.hpp), micro-os-plus::devices-storage-sd (sd.hpp only) and
# micro-os-plus::devices-storage-posix (the SD card as a posix-io block device).
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

# The SD card interface alone (sd.hpp): for a consumer that brings the card
# driver and wants none of the file systems above.
if (NOT TARGET micro-os-plus::devices-storage-sd)
  add_library (micro-os-plus-iii-devices-storage-sd-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-storage-sd-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  add_library (micro-os-plus::devices-storage-sd
               ALIAS micro-os-plus-iii-devices-storage-sd-interface)
endif ()

# The SD card as a µOS++ POSIX I/O block device (sd::block_device), for a file
# system on top (the @xpacks/chan-fatfs POSIX I/O integration). sd.hpp and the
# card driver come from the consumer (a device package's SD set, or the host
# back-end of -posix-arch), so this adds the adapter and the kernel's posix-io
# only.
if (NOT TARGET micro-os-plus::devices-storage-posix)
  add_library (micro-os-plus-iii-devices-storage-posix-interface INTERFACE)
  target_include_directories (
    micro-os-plus-iii-devices-storage-posix-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/posix-io/include"
  )
  target_sources (
    micro-os-plus-iii-devices-storage-posix-interface
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/posix-io/src/sd-block-device.cpp"
  )
  target_link_libraries (
    micro-os-plus-iii-devices-storage-posix-interface
    INTERFACE micro-os-plus::iii-posix-io
  )
  add_library (micro-os-plus::devices-storage-posix
               ALIAS micro-os-plus-iii-devices-storage-posix-interface)
endif ()
