# -----------------------------------------------------------------------------
# The WeAct RP2350B's architecture switch, a CTest fixture of each hardware
# test (CMakeLists.txt): are the RISC-V cores there? If not, program the
# test's image through the Arm view: the boot ROM reads its RISC-V IMAGE_DEF
# on the reset and switches both cores. The port's former hw.sh did the same
# before every run. When even the Arm view cannot reach the chip (an image in
# flash that keeps the debug port out, such as a clock test parked after its
# reboot), a rescue reset first (target/rp2350.cfg RESCUE: the whole chip
# except the debug port, the boot ROM stopping before the flash image), then
# the programming once more.
#
#   cmake -DOPENOCD=<exe> -DSCRIPTS=<dir> -DCFG=<openocd.cfg> -DELF=<image>
#         -DLOG=<log> -P arch-switch.cmake
# -----------------------------------------------------------------------------

cmake_minimum_required (VERSION 3.20)

foreach (_v OPENOCD SCRIPTS CFG ELF LOG)
  if (NOT DEFINED ${_v})
    message (FATAL_ERROR "arch-switch.cmake: ${_v} is not set.")
  endif ()
endforeach ()

execute_process (
  COMMAND "${OPENOCD}" -s "${SCRIPTS}" -f "${CFG}" -f target/rp2350-riscv.cfg
          -c "init" -c "shutdown"
  OUTPUT_VARIABLE _out ERROR_VARIABLE _out TIMEOUT 60)
file (WRITE "${LOG}" "${_out}")

if (_out MATCHES "Examined RISC-V core")
  message (STATUS "the RISC-V cores are running")
  return ()
endif ()

message (STATUS "the RISC-V cores are not there: programming through the Arm view to switch")
foreach (_try 1 2)
  execute_process (
    COMMAND "${OPENOCD}" -s "${SCRIPTS}" -f "${CFG}" -f target/rp2350.cfg
            -c "init" -c "reset init" -c "program {${ELF}} verify" -c "reset run"
            -c "shutdown"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _out TIMEOUT 120
    RESULT_VARIABLE _rc)
  file (APPEND "${LOG}" "${_out}")
  if (_rc EQUAL 0 OR _try EQUAL 2)
    break ()
  endif ()
  message (STATUS "the Arm view cannot reach the chip: rescue reset, then again")
  execute_process (
    COMMAND "${OPENOCD}" -s "${SCRIPTS}" -f "${CFG}" -c "set RESCUE 1"
            -f target/rp2350.cfg -c "init" -c "shutdown"
    OUTPUT_VARIABLE _out ERROR_VARIABLE _out TIMEOUT 60)
  file (APPEND "${LOG}" "${_out}")
endforeach ()
if (NOT _rc EQUAL 0)
  message (FATAL_ERROR "the switch through the Arm view failed, a rescue reset "
                       "too (see ${LOG}).")
endif ()
execute_process (COMMAND "${CMAKE_COMMAND}" -E sleep 1)
