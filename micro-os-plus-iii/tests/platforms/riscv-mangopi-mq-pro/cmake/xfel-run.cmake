# -----------------------------------------------------------------------------
# One MangoPi MQ-Pro hardware test, over xfel (USB FEL) only: the board's
# loader, run by CTest (CMakeLists.txt). The port's former
# test/boards/mangopi-mq-pro/hw.sh, without the shell:
#
#   cmake -DXFEL=<xfel> -DOBJCOPY=<objcopy> -DREADELF=<readelf> -DELF=<image>
#         -DWORK=<folder> -DSECS=<seconds> -P xfel-run.cmake
#
# The board must be in FEL mode (USB-C OTG cable unplugged, microSD card out,
# plugged back in). xfel initialises the DDR, writes the flat image at
# 0x40000000 and jumps to its entry. There is no debugger: every test ends by
# resetting into FEL (riscv_board_test_finish(), from hw_result, a returning
# os_main or a trap), leaving its verdict in the battery-backed RTC:
#
#   RTC GP_DATA3 (0x0709010c) = 0x4d515052 ("MQPR")
#   RTC GP_DATA4 (0x07090110) = 0 PASS, 1 FAIL, 2 SKIP
#
# This prints the matching RESULT line, which CTest reads, then the console
# log the test kept in RAM (the board's blackbox, read over xfel). UART0 is
# left to the user: it is never opened.
#
# UOS_MQPRO_NORESET=1 in the environment: no xfel reset before the run (the
# boot ROM would boot an inserted microSD card; a card-present test is started
# with the board already in FEL from the card's own image).
# -----------------------------------------------------------------------------

foreach (_v XFEL OBJCOPY READELF ELF WORK SECS)
  if (NOT DEFINED ${_v})
    message (FATAL_ERROR "xfel-run.cmake: ${_v} is not set.")
  endif ()
endforeach ()

set (_result_reg 0x0709010C)
set (_result_code 0x07090110)
set (_magic 1297174610) # 0x4d515052
set (_bb_base 0x58000000)
set (_bb_text 0x58000010)
set (_bb_max 65536)

file (MAKE_DIRECTORY "${WORK}")

# xfel <args...>; rc and output in _x_rc, _x_out.
macro (_xfel)
  execute_process (COMMAND "${XFEL}" ${ARGN}
                   OUTPUT_VARIABLE _x_out ERROR_VARIABLE _x_out
                   RESULT_VARIABLE _x_rc TIMEOUT 5)
endmacro ()

# Wait up to <seconds> for FEL to answer; _fel is TRUE when it did.
function (_wait_fel _seconds)
  string (TIMESTAMP _t0 "%s")
  set (_fel FALSE PARENT_SCOPE)
  while (TRUE)
    execute_process (COMMAND "${XFEL}" version OUTPUT_QUIET ERROR_QUIET
                     RESULT_VARIABLE _rc TIMEOUT 1)
    if (_rc STREQUAL "0")
      set (_fel TRUE PARENT_SCOPE)
      return ()
    endif ()
    string (TIMESTAMP _t "%s")
    math (EXPR _dt "${_t} - ${_t0}")
    if (_dt GREATER_EQUAL _seconds)
      return ()
    endif ()
    execute_process (COMMAND "${CMAKE_COMMAND}" -E sleep 0.5)
  endwhile ()
endfunction ()

# An xfel read32 value (its last word), as a decimal number, in <out>.
function (_read32 _addr _out)
  execute_process (COMMAND "${XFEL}" read32 ${_addr}
                   OUTPUT_VARIABLE _o ERROR_QUIET TIMEOUT 5)
  set (${_out} "" PARENT_SCOPE)
  if (_o MATCHES "(0x[0-9a-fA-F]+)[ \t\r\n]*$")
    math (EXPR _v "${CMAKE_MATCH_1}")
    set (${_out} "${_v}" PARENT_SCOPE)
  endif ()
endfunction ()

# FEL must answer.
_wait_fel (5)
if (NOT _fel)
  message (FATAL_ERROR
    "the board is not in FEL. Unplug the USB-C OTG cable, remove the microSD "
    "card, plug it back into the OTG port, then retry. A test that is still "
    "running never returns to the boot ROM by itself.")
endif ()

# Every test starts from a clean, xfel-reset system.
if (NOT "$ENV{UOS_MQPRO_NORESET}" STREQUAL "1")
  _xfel (reset)
  _wait_fel (60)
  if (NOT _fel)
    message (FATAL_ERROR
      "the board did not come back to FEL after xfel reset (is the microSD "
      "card removed? UOS_MQPRO_NORESET=1 keeps it).")
  endif ()
endif ()

# Invalidate the verdict before the run.
_xfel (write32 ${_result_reg} 0)
_xfel (write32 ${_result_code} 0)

execute_process (COMMAND "${READELF}" -h "${ELF}" OUTPUT_VARIABLE _hdr)
if (NOT _hdr MATCHES "Entry point address: *(0x[0-9a-fA-F]+)")
  message (FATAL_ERROR "no entry point in ${ELF}.")
endif ()
set (_entry "${CMAKE_MATCH_1}")
set (_bin "${WORK}/image.bin")
execute_process (COMMAND "${OBJCOPY}" -O binary "${ELF}" "${_bin}"
                 RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0)
  message (FATAL_ERROR "objcopy failed.")
endif ()

message ("== MangoPi MQ-Pro: ${ELF} (entry ${_entry}; UART untouched) ==")
_xfel (ddr d1)
if (NOT _x_rc STREQUAL "0")
  message (FATAL_ERROR "xfel ddr failed:\n${_x_out}")
endif ()
execute_process (COMMAND "${XFEL}" write 0x40000000 "${_bin}"
                 OUTPUT_VARIABLE _x_out ERROR_VARIABLE _x_out
                 RESULT_VARIABLE _x_rc TIMEOUT 60)
if (NOT _x_rc STREQUAL "0")
  message (FATAL_ERROR "xfel write failed:\n${_x_out}")
endif ()
# exec does not return while the image runs; its timeout is expected.
_xfel (exec ${_entry})

# The test resets into FEL itself. Wait for it.
_wait_fel (${SECS})
if (NOT _fel)
  message (FATAL_ERROR
    "TIMEOUT after ${SECS} s: the test did not reset. Power-cycle the board "
    "(microSD card out) and retry.")
endif ()

_read32 (${_result_reg} _m)
_read32 (${_result_code} _c)

# The console log the test kept in RAM: magic 'BBLG', then the text,
# NUL-terminated. The test's reset took the DDR controller down: set it up
# again first.
set (_log "")
_xfel (ddr d1)
if (_x_rc STREQUAL "0")
  _read32 (${_bb_base} _bb)
  if (_bb STREQUAL "1111641159") # 0x42424C47
    set (_bb_file "${WORK}/blackbox.bin")
    execute_process (COMMAND "${XFEL}" read ${_bb_text} ${_bb_max} "${_bb_file}"
                     OUTPUT_QUIET ERROR_QUIET RESULT_VARIABLE _rc TIMEOUT 30)
    if (_rc STREQUAL "0")
      # CMake reads up to the first NUL: the end of the text.
      file (READ "${_bb_file}" _log)
      string (REPLACE "\r" "" _log "${_log}")
    endif ()
    if (_log STREQUAL "")
      set (_log "[blackbox: empty]")
    endif ()
  else ()
    set (_log "[blackbox: no log (the DDR did not keep it, or the image did not write one)]")
  endif ()
endif ()

# Leave a clean FEL for the next test.
_xfel (reset)
_wait_fel (60)

if (_log)
  message ("---- console log (from RAM, read over xfel) ----\n${_log}\n---- end of log ----")
endif ()

if (_m STREQUAL "" OR _c STREQUAL "")
  message (FATAL_ERROR "the test reset, but there is no RTC verdict.")
elseif (NOT _m EQUAL _magic)
  message (FATAL_ERROR "the test reset, but the RTC verdict is invalid (magic ${_m}).")
elseif (_c EQUAL 0)
  message ("\nRESULT: PASS (RTC)")
elseif (_c EQUAL 2)
  message ("\nRESULT: SKIP (RTC)")
else ()
  message ("\nRESULT: FAIL (RTC code ${_c})")
endif ()
