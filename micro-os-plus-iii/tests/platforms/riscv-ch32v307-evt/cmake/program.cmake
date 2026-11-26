# -----------------------------------------------------------------------------
# The CH32V307V-EVT's programming, a CTest fixture of each hardware test
# (CMakeLists.txt): flash the image, read the flash back and compare it with
# the image, segment by segment (the port's former test/boards/ch32v307-evt/
# hw.sh did the same before every run).
#
#   cmake -DOPENOCD=<exe> -DSCRIPTS=<dir> -DREADELF=<readelf> -DELF=<image>
#         -DWORK=<folder> -P program.cmake
#
# The WCH OpenOCD (the MounRiver fork) has no usable verify_image here: it
# needs a work area, and the configuration's 0x80000000 is not RAM on this
# chip. So the flash is dumped and compared on the PC: each LOAD segment's
# FILE bytes at its load address (the code, then .data's initial values);
# gaps and memory-only tails are not written and are not compared. Never
# load_image below 0x20000000 with this OpenOCD: in WCH mode it treats every
# such write as flash data, into a 256-byte buffer, unchecked.
# -----------------------------------------------------------------------------

cmake_minimum_required (VERSION 3.20)

foreach (_v OPENOCD SCRIPTS READELF ELF WORK)
  if (NOT DEFINED ${_v})
    message (FATAL_ERROR "program.cmake: ${_v} is not set.")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${WORK}")
set (_back "${WORK}/back.bin")

# The flash segments: LOAD, file bytes > 0, load address below the SRAM.
execute_process (COMMAND "${READELF}" -lW "${ELF}" OUTPUT_VARIABLE _ph)
string (REPLACE "\n" ";" _lines "${_ph}")
set (_segs "")
set (_size 0)
foreach (_l IN LISTS _lines)
  if (_l MATCHES "^ *LOAD +0x([0-9a-f]+) +0x[0-9a-f]+ +0x([0-9a-f]+) +0x([0-9a-f]+) ")
    math (EXPR _off "0x${CMAKE_MATCH_1}")
    math (EXPR _paddr "0x${CMAKE_MATCH_2}")
    math (EXPR _filesz "0x${CMAKE_MATCH_3}")
    if (_filesz GREATER 0 AND _paddr LESS 536870912) # 0x20000000
      list (APPEND _segs "${_off}:${_paddr}:${_filesz}")
      math (EXPR _end "${_paddr} + ${_filesz}")
      if (_end GREATER _size)
        set (_size ${_end})
      endif ()
    endif ()
  endif ()
endforeach ()
if (NOT _segs)
  message (FATAL_ERROR "no flash segments in ${ELF}.")
endif ()

set (_ocd "${OPENOCD}" -s "${SCRIPTS}" -f interface/wlink.cfg -f target/wch-riscv.cfg)

function (_compare _ok)
  set (${_ok} TRUE PARENT_SCOPE)
  foreach (_s IN LISTS _segs)
    string (REPLACE ":" ";" _f "${_s}")
    list (GET _f 0 _off)
    list (GET _f 1 _paddr)
    list (GET _f 2 _filesz)
    file (READ "${ELF}" _want OFFSET ${_off} LIMIT ${_filesz} HEX)
    file (READ "${_back}" _got OFFSET ${_paddr} LIMIT ${_filesz} HEX)
    if (NOT _want STREQUAL _got)
      message ("the flash differs from the image in the segment at ${_paddr} (${_filesz} bytes)")
      set (${_ok} FALSE PARENT_SCOPE)
      return ()
    endif ()
  endforeach ()
endfunction ()

message (STATUS "programming ${_size} bytes (the whole chip is erased)")
execute_process (
  COMMAND ${_ocd} -c "init" -c "reset halt"
          -c "flash write_image erase {${ELF}}"
          -c "dump_image {${_back}} 0x00000000 ${_size}" -c "shutdown"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _out RESULT_VARIABLE _rc TIMEOUT 120)
file (WRITE "${WORK}/program.log" "${_out}")
if (NOT _rc EQUAL 0)
  message (FATAL_ERROR "OpenOCD failed while programming (see ${WORK}/program.log).")
endif ()

_compare (_ok)
if (NOT _ok)
  # Measured once: one byte differed, and the next read-backs of the same
  # flash were identical -- a read error over the link, not a bad write.
  message ("reading the flash back a second time")
  execute_process (
    COMMAND ${_ocd} -c "init" -c "halt"
            -c "dump_image {${_back}} 0x00000000 ${_size}" -c "shutdown"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _out TIMEOUT 60)
  file (WRITE "${WORK}/readback.log" "${_out}")
  _compare (_ok)
  if (NOT _ok)
    message (FATAL_ERROR "the flash read back differs from the image.")
  endif ()
endif ()
message (STATUS "flash programmed and read back identical")
