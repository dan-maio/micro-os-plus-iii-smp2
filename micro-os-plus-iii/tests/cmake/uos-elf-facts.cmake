# -----------------------------------------------------------------------------
# This file is part of the µOS++ project (https://micro-os-plus.github.io/).
# Copyright (c) 2026 Liviu Ionescu. All rights reserved.
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose is hereby granted, under the terms of the MIT license.
#
# If a copy of the license was not distributed with this file, it can be
# obtained from https://opensource.org/licenses/mit.
# -----------------------------------------------------------------------------

# What a board's hardware session needs to know about one image, as Tcl the
# session sources (a POST_BUILD step of the image; the platform's
# CMakeLists.txt):
#
#   cmake -DREADELF=<readelf> -DNM=<nm> -DELF=<image> -DOUT=<file.tcl>
#         -DSYMBOLS=<name,name,...> -P uos-elf-facts.cmake
#
# writes
#
#   set uos_elf(entry) 0x...           the ELF entry point
#   set uos_elf(<name>) 0x...          each symbol found
#   set uos_elf(<name>,size) 0x...     and its size (nm -S), when it has one
#
# A symbol the image does not have is left out ([info exists] tells).

foreach (_v READELF NM ELF OUT)
  if (NOT DEFINED ${_v})
    message (FATAL_ERROR "uos-elf-facts.cmake: ${_v} is not set.")
  endif ()
endforeach ()

execute_process (COMMAND "${READELF}" -h "${ELF}"
                 OUTPUT_VARIABLE _hdr RESULT_VARIABLE _rc)
if (NOT _rc EQUAL 0 OR NOT _hdr MATCHES "Entry point address: *(0x[0-9a-fA-F]+)")
  message (FATAL_ERROR "uos-elf-facts.cmake: no entry point in ${ELF}.")
endif ()
set (_tcl "# ${ELF}\nset uos_elf(entry) ${CMAKE_MATCH_1}\n")

if (DEFINED SYMBOLS AND NOT SYMBOLS STREQUAL "")
  string (REPLACE "," ";" _symbols "${SYMBOLS}")
  execute_process (COMMAND "${NM}" -S "${ELF}" OUTPUT_VARIABLE _nm)
  string (REPLACE "\n" ";" _lines "${_nm}")
  foreach (_sym IN LISTS _symbols)
    foreach (_l IN LISTS _lines)
      # "address [size] type name"
      if (_l MATCHES "^([0-9a-fA-F]+) ([0-9a-fA-F]+) [A-Za-z] ${_sym}$")
        string (APPEND _tcl "set uos_elf(${_sym}) 0x${CMAKE_MATCH_1}\n")
        string (APPEND _tcl "set uos_elf(${_sym},size) 0x${CMAKE_MATCH_2}\n")
        break ()
      elseif (_l MATCHES "^([0-9a-fA-F]+) [A-Za-z] ${_sym}$")
        string (APPEND _tcl "set uos_elf(${_sym}) 0x${CMAKE_MATCH_1}\n")
        break ()
      endif ()
    endforeach ()
  endforeach ()
endif ()

file (WRITE "${OUT}" "${_tcl}")
