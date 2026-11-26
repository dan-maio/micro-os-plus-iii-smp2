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

# A hardware test's OpenOCD session, run again when the attach failed
# (uos_tests_add_hwd's RETRY_*; cmake/uos-tests-run.cmake):
#
#   cmake -DOPENOCD=<exe> -DARGS=<arguments joined with '|'> -DTRIES=<n>
#         -DRETRY_IF=<regex> [-DRETRY_UNLESS=<regex>] -P uos-hwd-retry.cmake
#
# Some probes read a garbage scan chain on the first attach after another
# OpenOCD session (the Milk-V Duo's Sipeed CMSIS-DAP: every other time; the
# next attach works). Such a session fails before the core is examined, so
# its output matches RETRY_IF and not RETRY_UNLESS, and OpenOCD is started
# again, up to TRIES times in all. The output of every session is shown as it
# comes (CTest reads the test's RESULT line from it); the script fails when
# the last session's exit status, the test's semihosting exit code when the
# test ended the session, is not 0.

foreach (_v OPENOCD ARGS TRIES RETRY_IF)
  if (NOT DEFINED ${_v})
    message (FATAL_ERROR "uos-hwd-retry.cmake: ${_v} is not set.")
  endif ()
endforeach ()

string (REPLACE "|" ";" _args "${ARGS}")

foreach (_try RANGE 1 ${TRIES})
  execute_process (
    COMMAND "${OPENOCD}" ${_args}
    OUTPUT_VARIABLE _out ERROR_VARIABLE _out
    ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE
    RESULT_VARIABLE _rc)
  set (_failed FALSE)
  if (_out MATCHES "${RETRY_IF}")
    set (_failed TRUE)
    if (DEFINED RETRY_UNLESS AND NOT RETRY_UNLESS STREQUAL ""
        AND _out MATCHES "${RETRY_UNLESS}")
      set (_failed FALSE)
    endif ()
  endif ()
  if (NOT _failed OR _try EQUAL TRIES)
    break ()
  endif ()
  message ("-- attach ${_try} of ${TRIES} failed (scan chain); starting OpenOCD again")
endforeach ()

# The test's own verdict is its RESULT line (CTest's regular expressions); a
# session that ended otherwise fails here too. (CMake 3.29's
# cmake_language (EXIT) would keep the exact code; the configurations pin an
# older CMake.)
if (NOT _rc STREQUAL "0")
  message (FATAL_ERROR "OpenOCD ended with ${_rc}.")
endif ()
