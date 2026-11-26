# -----------------------------------------------------------------------------
# What the VisionFive 2 Lite's tests need beyond the defaults.
#
# Every test is a board test (own os_main(), links micro-os-plus::riscv, ends
# with a RESULT line and a semihosting exit), built only as a hardware image
# (board.cmake sets no QEMU linker) that runs entirely in DDR.
#
# The tests are the portable ones of the Milk-V Duo, adapted where the board
# differs: the clock is clint::now () (the CLINT's mtime -- the U74 has no
# time CSR), and mmu-test uses this board's map and skips the misaligned
# accesses the U74 traps on.
# -----------------------------------------------------------------------------

# SMP: the board has four U74 harts (UOS_BOARD_NCPU 4, CPU n = mhartid n+1).
# EVERY test runs the kernel's SMP scheduler on all four.
#
#   - smp-test2 and smp-mat-test start CPUs 1..3 themselves (their own
#     idle threads and multicore::launch_core calls, as on the Pico 2);
#   - every other test gets src/smp-autostart.cpp: its os_main() is renamed
#     vf2_test_os_main() (-D on the test's own sources only, so the kernel's
#     main() still calls the board's os_main()), and smp-autostart's
#     os_main() starts CPUs 1..3, checks that each runs on its own hart, and
#     then calls the test. A core that does not start fails the test.
set (_vf2_self_launching_tests smp-test2 smp-mat-test)

# -----------------------------------------------------------------------------
# Upstream's suites (UOS_TESTS_SUITES, in ../CMakeLists.txt) run on all four
# U74 too: the platform's start-up (../src/platform-support.cpp) starts CPUs
# 1..3 with vf2_smp_start() (src/smp-autostart.cpp) before the suite runs.

# Neither the self-launching tests nor the suites get the auto-start rename.
set (_vf2_no_autostart ${_vf2_self_launching_tests} ${UOS_TESTS_SUITES})

function (board_test_ncpu _app _out)
  set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
endfunction ()

# -----------------------------------------------------------------------------
# emmc-fs-test: FatFs (Chan, 0.13.1, copied from the xPack the kernel
# harness uses) -- its core only, in emmc-fs-test/fatfs/ (ff.c, ffunicode.c,
# the headers; ffconf.h without the xPack's FF_FS_POSIX_INTEGRATION). The
# test brings the disk layer (the eMMC area) and get_fattime().
set (_vf2_fatfs_tests emmc-fs-test)
set (_vf2_fatfs_dir "${CMAKE_CURRENT_LIST_DIR}/emmc-fs-test/fatfs")

# The USB device test: TinyUSB with the board's Cadence driver and the CDC
# glue (board.cmake UOS_BOARD_USB_*). Its tusb_config.h directory goes first
# on the include path.
set (_vf2_usb_cdc_tests smp-test-usb-cdc-acm)
set (_vf2_usb_cdc_raw_tests smp-test-usb-cdc-file)
# USB file round trip through a FAT file on the eMMC: USB + FatFs.
set (_vf2_usb_fs_tests smp-test-usb-cdc-file-fs)

# USB CDC console *and* a real network: TinyUSB + GMAC0 + lwIP over it.
set (_vf2_usb_net_tests smp-test-usb-cdc-net)
# smp-net-ram-disk: the same GMAC0 + lwIP stack (Ethernet only, no Wi-Fi, no
# USB console), plus the 512 MiB RAM disk and the file server shared with
# vf2-wifi-ram-disk (ram-disk.cpp, file-server.cpp in that directory) and
# FatFs.  The USB core sources stay in the link only for vf2_dcache_flush()
# (usb/vf2-usb.cpp), which the GMAC's DMA cache maintenance uses.
set (_vf2_net_ramdisk_tests smp-net-ram-disk)
set (_vf2_ramdisk_dir "${CMAKE_CURRENT_LIST_DIR}/vf2-wifi-ram-disk")
set (_vf2_net_dir "${CMAKE_SOURCE_DIR}/platforms/riscv-visionfive2-lite/net")
set (_vf2_lwip_dir "${CMAKE_SOURCE_DIR}/third_party/lwip")

set (NET_TEST_INHERIT_GMAC OFF CACHE BOOL
     "smp-test-usb-cdc-net: use the GMAC bring-up U-Boot already did")
set (NET_TEST_STAY_ALIVE OFF CACHE BOOL
     "smp-test-usb-cdc-net: keep running after the verdict (JTAG bring-up)")
set (NET_RAMDISK_FALLBACK_IP "" CACHE STRING
     "smp-net-ram-disk: diagnostics, static address after a failed DHCP (e.g. 0xC0A801FA)")
set (NET_RAMDISK_DHCP_TRACE OFF CACHE BOOL
     "smp-net-ram-disk: trace the DHCP exchange (lwIP DHCP_DEBUG + received DHCP frames)")
set (NET_TEST_STATIC_IP "" CACHE STRING
     "smp-test-usb-cdc-net: diagnostics, a fixed address instead of DHCP (e.g. 0xC0A801FA = 192.168.1.250/24)")
set (NET_TEST_PEER "" CACHE STRING
     "smp-test-usb-cdc-net: optional second host on the LAN to ping")

# smp-wifi: the on-board AIC8800 Wi-Fi module on SDIO1 + lwIP.  Everything it
# needs beyond the port lives in its own directory (the SDIO1 host, the chip
# and 802.11 layers, the lwIP glue, the firmware blobs) -- not under
# test/boards/visionfive2-lite/, whose sources every other test's image is
# checked against (hw.sh refuses an image older than them).  Shared with the
# rest of the tree: lwIP itself, this board's lwipopts.h / arch/cc.h, and the
# MangoPi MQ-Pro's header-only WPA2 crypto.
set (_vf2_wifi_tests smp-wifi vf2-wifi-ram-disk)
set (_vf2_wifi_dir "${CMAKE_CURRENT_LIST_DIR}/smp-wifi")
set (_vf2_wifi_crypto_dir
     "${CMAKE_SOURCE_DIR}/platforms/riscv-mangopi-mq-pro/wifi/src/wifi")

# The AIC8800 firmware is not in the repository (smp-wifi/third_party/aic8800/
# README.md says where it comes from); without it the Wi-Fi tests are left out.
foreach (_fw fmacfw_8800d80_u02.bin fmacfw_8800d80_h_u02.bin)
  if (NOT EXISTS "${_vf2_wifi_dir}/third_party/aic8800/${_fw}")
    message (STATUS "${PLATFORM_NAME}: ${_fw} is missing, ${_vf2_wifi_tests} not built (smp-wifi/third_party/aic8800/README.md)")
    list (APPEND BOARD_TEST_EXCLUDE ${_vf2_wifi_tests})
    break ()
  endif ()
endforeach ()

# vf2-wifi-ram-disk: smp-wifi's radio, WPA2 and lwIP code, unchanged, plus a
# 512 MiB RAM disk (FatFs, the copy under emmc-fs-test/fatfs) and a TCP file
# server.  Its own sources are in its directory; it reads smp-wifi's headers
# (net/, wifi/, wifi-config.hpp) through that directory on the include path.
set (_vf2_wifi_ramdisk_tests vf2-wifi-ram-disk)

# What smp-wifi associates with.  No credentials in the repository: pass them
# on the cmake line (the same entries as the MangoPi MQ-Pro's wifi-net), e.g.
#   -DWIFI_TEST_SSID=myssid -DWIFI_TEST_PASS=mypassphrase
set (WIFI_TEST_SSID "" CACHE STRING "smp-wifi: SSID to join")
set (WIFI_TEST_PASS "" CACHE STRING "smp-wifi: WPA2 passphrase (empty: open)")
set (WIFI_TEST_GATEWAY "gw" CACHE STRING
     "smp-wifi: what to ping (gw = the DHCP gateway, or a dotted address)")
set (WIFI_TEST_PEER "" CACHE STRING "smp-wifi: optional second host to ping")

function (board_test_sources _app _out)
  if ("${_app}" IN_LIST _vf2_wifi_tests)
    set (_extra "")
    if ("${_app}" IN_LIST _vf2_wifi_ramdisk_tests)
      set (_extra "${_vf2_fatfs_dir}/source/ff.c"
                  "${_vf2_fatfs_dir}/source/ffunicode.c")
    endif ()
    file (GLOB _wifi_lwip CONFIGURE_DEPENDS
          "${_vf2_lwip_dir}/src/core/*.c"
          "${_vf2_lwip_dir}/src/core/ipv4/*.c"
          "${_vf2_lwip_dir}/src/netif/*.c")
    set (${_out}
         "${_vf2_wifi_dir}/wifi/sdio1.cpp"
         "${_vf2_wifi_dir}/wifi/aic8800.cpp"
         "${_vf2_wifi_dir}/wifi/aic8800-fw.S"
         "${_vf2_wifi_dir}/wifi/wlan.cpp"
         "${_vf2_wifi_dir}/net/wifi-lwip.cpp"
         ${_wifi_lwip}
         ${_extra}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_net_ramdisk_tests)
    file (GLOB _net_lwip CONFIGURE_DEPENDS
          "${_vf2_lwip_dir}/src/core/*.c"
          "${_vf2_lwip_dir}/src/core/ipv4/*.c"
          "${_vf2_lwip_dir}/src/netif/*.c")
    set (${_out}
         ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_RAW_SOURCES}
         "${_vf2_net_dir}/gmac.cpp" "${_vf2_net_dir}/net-lwip.cpp"
         ${_net_lwip}
         "${_vf2_ramdisk_dir}/ram-disk.cpp" "${_vf2_ramdisk_dir}/file-server.cpp"
         "${_vf2_fatfs_dir}/source/ff.c" "${_vf2_fatfs_dir}/source/ffunicode.c"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_net_tests)
    file (GLOB _net_lwip CONFIGURE_DEPENDS
          "${_vf2_lwip_dir}/src/core/*.c"
          "${_vf2_lwip_dir}/src/core/ipv4/*.c"
          "${_vf2_lwip_dir}/src/netif/*.c")
    set (${_out}
         ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES}
         "${_vf2_net_dir}/gmac.cpp" "${_vf2_net_dir}/net-lwip.cpp"
         ${_net_lwip}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_fatfs_tests)
    set (${_out} "${_vf2_fatfs_dir}/source/ff.c" "${_vf2_fatfs_dir}/source/ffunicode.c"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_cdc_tests)
    set (${_out} ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_SOURCES}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_cdc_raw_tests)
    set (${_out} ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_RAW_SOURCES}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_fs_tests)
    set (${_out} ${UOS_BOARD_USB_CORE_SOURCES} ${UOS_BOARD_USB_CDC_RAW_SOURCES}
         "${_vf2_fatfs_dir}/source/ff.c" "${_vf2_fatfs_dir}/source/ffunicode.c"
         PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_includes _app _out)
  if ("${_app}" IN_LIST _vf2_wifi_ramdisk_tests)
    set (${_out}
         "${_vf2_wifi_dir}"
         "${_vf2_fatfs_dir}/include"
         "${_vf2_wifi_crypto_dir}"
         "${_vf2_net_dir}"
         "${_vf2_lwip_dir}/src/include"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_wifi_tests)
    set (${_out}
         "${_vf2_wifi_crypto_dir}"
         "${_vf2_net_dir}"
         "${_vf2_lwip_dir}/src/include"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_net_ramdisk_tests)
    set (${_out}
         ${UOS_BOARD_USB_CDC_INCLUDE} ${UOS_BOARD_USB_INCLUDE}
         "${_vf2_ramdisk_dir}"
         "${_vf2_fatfs_dir}/include"
         "${_vf2_net_dir}"
         "${_vf2_lwip_dir}/src/include"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_net_tests)
    set (${_out}
         ${UOS_BOARD_USB_CDC_INCLUDE} ${UOS_BOARD_USB_INCLUDE}
         "${_vf2_net_dir}"
         "${_vf2_lwip_dir}/src/include"
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_fatfs_tests)
    set (${_out} "${_vf2_fatfs_dir}/include" PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_cdc_tests OR "${_app}" IN_LIST _vf2_usb_cdc_raw_tests)
    set (${_out} ${UOS_BOARD_USB_CDC_INCLUDE} ${UOS_BOARD_USB_INCLUDE}
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_fs_tests)
    set (${_out} ${UOS_BOARD_USB_CDC_INCLUDE} ${UOS_BOARD_USB_INCLUDE}
         "${_vf2_fatfs_dir}/include" PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_defines _app _out)
  if ("${_app}" IN_LIST _vf2_wifi_tests)
    set (${_out} VF2_SMP_AUTOSTART=1
         "WIFI_TEST_SSID=\"${WIFI_TEST_SSID}\""
         "WIFI_TEST_PASS=\"${WIFI_TEST_PASS}\""
         "WIFI_TEST_GATEWAY=\"${WIFI_TEST_GATEWAY}\""
         "WIFI_TEST_PEER=\"${WIFI_TEST_PEER}\""
         PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_net_ramdisk_tests)
    set (_d VF2_SMP_AUTOSTART=1)
    if (NET_RAMDISK_DHCP_TRACE)
      # lwIP's own DHCP trace (LWIP_DEBUGF -> kprintf -> the console log)
      # and the test's decoder of every DHCP frame received.
      list (APPEND _d NET_RAMDISK_DHCP_TRACE=1 LWIP_DEBUG=1
                      DHCP_DEBUG=LWIP_DBG_ON)
    endif ()
    if (NET_RAMDISK_FALLBACK_IP)
      # Diagnostics: after a failed DHCP, carry on with this address (given
      # as 0xC0A801FA for 192.168.1.250); the verdict stays FAIL.
      list (APPEND _d NET_RAMDISK_FALLBACK_IP=${NET_RAMDISK_FALLBACK_IP})
    endif ()
    set (${_out} ${_d} PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_usb_net_tests)
    set (_d VF2_SMP_AUTOSTART=1 "NET_TEST_PEER=\"${NET_TEST_PEER}\"")
    if (NET_TEST_STAY_ALIVE)
      list (APPEND _d NET_TEST_STAY_ALIVE=1)
    endif ()
    if (NET_TEST_INHERIT_GMAC)
      list (APPEND _d NET_TEST_INHERIT_GMAC=1)
    endif ()
    if (NET_TEST_STATIC_IP)
      list (APPEND _d NET_TEST_STATIC_IP=${NET_TEST_STATIC_IP})
    endif ()
    set (${_out} ${_d} PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_no_autostart)
    set (${_out} "" PARENT_SCOPE)
  else ()
    set (${_out} VF2_SMP_AUTOSTART=1 PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_options _app _out)
  if ("${_app}" STREQUAL "smp-mat-test")
    # The RP2350B-sized problem on the four U74s: N=320, B=40 -> 8 row
    # blocks, two per core (a multiple of OS_NCPU=4).
    set (${_out} -DSMP_MAT_N=320 -DSMP_MAT_B=40 PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _vf2_no_autostart)
    set (${_out} "" PARENT_SCOPE)
  else ()
    set (${_out} -Dos_main=vf2_test_os_main PARENT_SCOPE)
  endif ()
endfunction ()
