# -----------------------------------------------------------------------------
# tests.cmake -- the WeAct RP2350B (RISC-V) test set: the Cortex-M
# pico2-rp2350b-psram board's tests, on the Hazard3 cores.
#
# The board shares the Pico 2's sources; this file says which tests run on
# two harts, which are hardware only, which run without a kernel, and which
# keep their data in the 8 MB PSRAM (linker-psram.ld, PICO2_PSRAM).
# -----------------------------------------------------------------------------

set (_board_smp_tests smp-test-xip smp-test-xip-core1park smp-test-xip-fs smp-test2 smp-test3 smp-test4 smp-test5 smp-test-ko
     smp-test-nested smp-mat-test smp-mat-test_200 smp-mat-test_250 smp-test-nested-clock
     smp-test-nested-clock_200 smp-test-nested-clock_250)

# Tests that program RP2350 peripherals directly (TIMER0, SIO, ADC, QMI),
# and the bare one: hardware only, as on the Cortex-M board.
set (BOARD_TEST_HWD_ONLY psram-exec smp-test-xip smp-test-xip-core1park smp-test-xip-fs smp-test0 smp-test3 smp-test4 smp-test5 smp-test-ko
     smp-test-nested smp-mat-test smp-mat-test_200 smp-mat-test_250 smp-test-nested-clock
     smp-test-nested-clock_200 smp-test-nested-clock_250)

# Bare-metal probes: no kernel (micro-os-plus::riscv-bare), their own main().
set (BOARD_TEST_NO_KERNEL smp-test0 psram-exec)

# Data in the 8 MB PSRAM (QMI chip-select 1 = GPIO0).
set (_psram_tests smp-mat-test smp-mat-test_200 smp-mat-test_250 smp-test-nested-clock
     smp-test-nested-clock_200 smp-test-nested-clock_250)

# The memory maps are the platform's (device/linker-scripts/).
set (_psram_linker "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2-rp2350b-psram/device/linker-scripts/linker-psram.ld")

# The flash tests (from the predecessor micro-os-plus-iii-pico2, not in the
# Cortex-M repo): the image in the first 1 MiB, a flat file system after it.
set (_flatfs_tests smp-test-xip smp-test-xip-core1park smp-test-xip-fs)
set (_flatfs_linker "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2-rp2350b-psram/device/linker-scripts/linker-flatfs.ld")

# psram-exec (the Cortex-M pico2-pizero board's): a bare supervisor running
# two applications from the PSRAM; its own linker script, and the QMI init
# copied at start-up but called by main() (PICO2_PSRAM_COPY).
set (_psram_exec_linker "${CMAKE_SOURCE_DIR}/platforms/riscv-pico2-rp2350b-psram/device/linker-scripts/linker-psram-exec.ld")

# The three that drive the clock tree themselves (potentiometer -> SYS_TABLE
# row -> watchdog reboot) and read the ADC: the clock comes from the watchdog
# scratch, 48 MHz on a cold boot; _200 stops at 200 MHz, the other two add
# the 250 MHz overclock row -- as the Cortex-M board builds them.
set (_clock_tests smp-test-nested-clock smp-test-nested-clock_200
     smp-test-nested-clock_250)

function (board_test_defines _app _out)
  set (_defs "")
  if ("${_app}" IN_LIST _psram_tests)
    list (APPEND _defs PICO2_PSRAM PICO2_PSRAM_CS_PIN=0)
  endif ()
  if (_app STREQUAL "psram-exec")
    list (APPEND _defs PICO2_PSRAM_COPY PICO2_PSRAM_CS_PIN=0)
  endif ()
  if ("${_app}" IN_LIST _clock_tests)
    list (APPEND _defs PICO2_CLOCK_FROM_SCRATCH)
    if (NOT _app STREQUAL "smp-test-nested-clock_200")
      list (APPEND _defs PICO2_ENABLE_250MHZ)
    endif ()
  endif ()
  # Fixed clocks (not Cortex-M tests): smp-mat-test at 200 and 250 MHz.
  if (_app STREQUAL "smp-mat-test_200")
    list (APPEND _defs PICO2_SYS_CLK_KHZ=200000)
  elseif (_app STREQUAL "smp-mat-test_250")
    list (APPEND _defs PICO2_SYS_CLK_KHZ=250000)
  endif ()
  set (${_out} "${_defs}" PARENT_SCOPE)
endfunction ()

function (board_test_linker _app _out)
  if ("${_app}" IN_LIST _psram_tests)
    set (${_out} "${_psram_linker}" PARENT_SCOPE)
  elseif ("${_app}" IN_LIST _flatfs_tests)
    set (${_out} "${_flatfs_linker}" PARENT_SCOPE)
  elseif (_app STREQUAL "psram-exec")
    set (${_out} "${_psram_exec_linker}" PARENT_SCOPE)
  else ()
    set (${_out} "" PARENT_SCOPE)
  endif ()
endfunction ()

function (board_test_ncpu _app _out)
  if ("${_app}" IN_LIST _board_smp_tests)
    set (${_out} "${UOS_BOARD_NCPU}" PARENT_SCOPE)
  else ()
    set (${_out} 1 PARENT_SCOPE)
  endif ()
endfunction ()
