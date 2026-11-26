# -----------------------------------------------------------------------------
# What the MangoPi MQ-Pro's tests need beyond the defaults.
#
# Every test is a board test: it has its own os_main(), links
# micro-os-plus::riscv and ends with a RESULT line.  On hardware that verdict
# comes back over UART0 and the test resets the SoC into FEL
# (include/hw_result.hpp); upstream's suites end the same way
# (UOS_TEST_END_HW_RESULT, ../CMakeLists.txt).
#
# The test set is the portable RISC-V one of the Milk-V Duo, adapted for the
# D1: one C906 hart, no USB device stack, no CV1800B timers/PLIC test. Only
# `hwd` images: the RISC-V tests run emulated as qemu-riscv64/qemu-riscv32.
# -----------------------------------------------------------------------------

# No JTAG probe on this setup: semihosting-test needs a debugger/emulator, so
# it is QEMU-only, and this platform, which has no QEMU images, builds none.
set (BOARD_TEST_QEMU_ONLY semihosting-test)

# wifi-net needs the RTL8723DS on SDC1 and a real access point: there is
# nothing for QEMU to model, so it has a hardware image only.
set (BOARD_TEST_HWD_ONLY wifi-net wifi-ram-disk)

# wifi-ram-disk: wifi-net's radio + lwIP stack, unchanged, plus the 512 MiB
# RAM disk (0x60000000, the upper half of the 1 GiB board) and the TCP file
# server shared with the VisionFive 2 Lite tests, and FatFs (the copy under
# that board's emmc-fs-test/fatfs).  The board has no Ethernet: Wi-Fi only.
set (_mqpro_ramdisk_dir "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/tests/vf2-wifi-ram-disk")
set (_mqpro_fatfs_dir "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/tests/emmc-fs-test/fatfs")

# The RTL8723D firmware is not in the repository
# (wifi/src/third_party/rtw88/README.md says where it comes from); without it
# the Wi-Fi tests are left out.
if (NOT EXISTS "${UOS_BOARD_SRC_DIR}/wifi/src/third_party/rtw88/rtw8723d_fw.bin")
  message (STATUS "${PLATFORM_NAME}: rtw8723d_fw.bin is missing, wifi-net and wifi-ram-disk not built (wifi/src/third_party/rtw88/README.md)")
  list (APPEND BOARD_TEST_EXCLUDE wifi-net wifi-ram-disk)
endif ()

# What wifi-net associates with.  No credentials in the repository: pass them
# on the cmake line, e.g.
#   -DWIFI_TEST_SSID=myssid -DWIFI_TEST_PASS=mypassphrase
# A build without them still runs the radio and the scan, and fails the join
# step with that reason in the log.
set (WIFI_TEST_SSID "" CACHE STRING "wifi-net: SSID to join")
set (WIFI_TEST_PASS "" CACHE STRING "wifi-net: WPA2 passphrase")
set (WIFI_TEST_GATEWAY "192.168.1.1" CACHE STRING "wifi-net: gateway to ping")
set (WIFI_TEST_PEER "" CACHE STRING "wifi-net: optional second host to ping")

function (board_test_sources _app _out)
  if ("${_app}" STREQUAL "wifi-net" OR "${_app}" STREQUAL "wifi-ram-disk")
    file (GLOB_RECURSE _wifi_sources CONFIGURE_DEPENDS
          "${UOS_BOARD_SRC_DIR}/wifi/src/*.cpp"
          "${UOS_BOARD_SRC_DIR}/wifi/src/*.c"
          "${UOS_BOARD_SRC_DIR}/wifi/src/wifi/rtw/*.S"
          "${CMAKE_SOURCE_DIR}/third_party/lwip/src/core/*.c"
          "${CMAKE_SOURCE_DIR}/third_party/lwip/src/core/ipv4/*.c"
          "${CMAKE_SOURCE_DIR}/third_party/lwip/src/netif/*.c")
    if ("${_app}" STREQUAL "wifi-ram-disk")
      list (APPEND _wifi_sources
            "${_mqpro_ramdisk_dir}/ram-disk.cpp"
            "${_mqpro_ramdisk_dir}/file-server.cpp"
            "${_mqpro_fatfs_dir}/source/ff.c"
            "${_mqpro_fatfs_dir}/source/ffunicode.c")
    endif ()
    set (${_out} ${_wifi_sources} PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_includes _app _out)
  if ("${_app}" STREQUAL "wifi-ram-disk")
    set (${_out}
         "${UOS_BOARD_SRC_DIR}/wifi/src"
         "${UOS_BOARD_SRC_DIR}/wifi/src/net"
         "${UOS_BOARD_SRC_DIR}/wifi/src/wifi"
         "${UOS_BOARD_SRC_DIR}/wifi/src/wifi/rtw"
         "${CMAKE_SOURCE_DIR}/third_party/lwip/src/include"
         "${_mqpro_ramdisk_dir}"
         "${_mqpro_fatfs_dir}/include"
         PARENT_SCOPE)
  elseif ("${_app}" STREQUAL "wifi-net")
    set (${_out}
         "${UOS_BOARD_SRC_DIR}/wifi/src"
         "${UOS_BOARD_SRC_DIR}/wifi/src/net"
         "${UOS_BOARD_SRC_DIR}/wifi/src/wifi"
         "${UOS_BOARD_SRC_DIR}/wifi/src/wifi/rtw"
         "${CMAKE_SOURCE_DIR}/third_party/lwip/src/include"
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_defines _app _out)
  if ("${_app}" STREQUAL "wifi-net" OR "${_app}" STREQUAL "wifi-ram-disk")
    set (${_out}
         "WIFI_TEST_SSID=\"${WIFI_TEST_SSID}\""
         "WIFI_TEST_PASS=\"${WIFI_TEST_PASS}\""
         "WIFI_TEST_GATEWAY=\"${WIFI_TEST_GATEWAY}\""
         "WIFI_TEST_PEER=\"${WIFI_TEST_PEER}\""
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

# The single-hart smp-mat run with the requested problem size: 8 row blocks
# of 40 (N/B = 8, a multiple of OS_NCPU=1).
function (board_test_options _app _out)
  if ("${_app}" STREQUAL "smp-mat-test")
    set (${_out} -DSMP_MAT_N=320 -DSMP_MAT_B=40 PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()
