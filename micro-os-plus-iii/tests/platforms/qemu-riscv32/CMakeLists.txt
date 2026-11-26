# -----------------------------------------------------------------------------
# Test executables — µOS++ III RISC-V on QEMU's virt machine (RV32).
#
# The RISC-V board tests (platforms/riscv-milkv-duo/tests/, the tests that use
# only the board interface the virt machine has too) and upstream's suites
# (tests/sources/<suite>/), each built by the tests' builder (board-tests/) as
# one `qemu` image and registered as `<platform>-<app>-qemu`.
#
# The virt machine: qemu-system-riscv32, one hart, 128 MiB, no firmware -- the
# image is the reset vector. The console is the virt UART on stdio; each test
# ends itself through the semihosting SYS_EXIT, so CTest reads a real exit
# code, and the RESULT line it prints says FAIL or SKIP where that applies.
# -----------------------------------------------------------------------------

message (VERBOSE "Processing 'tests/platforms/${PLATFORM_NAME}'...")

# -----------------------------------------------------------------------------
include ("cmake/platform-library.cmake")

set (UOS_TESTS_PORT riscv)
# Upstream's suites, on the common end of a portable test
# (board-tests/portable-test-end.cpp), under the kernel's scalar scheduler.
set (UOS_TESTS_SUITES
     blinky fp-switch mutex-stress instrumentation rtos-apis cmsis-os-validator)
# The RISC-V board tests. They live with the board they were written for; the
# ones that need its silicon are listed HWD_ONLY there and get no image here.
set (UOS_TESTS_BOARD_DIR "${CMAKE_SOURCE_DIR}/platforms/riscv-milkv-duo/tests")
add_subdirectory ("${CMAKE_SOURCE_DIR}/board-tests" "port-tests/test")

set (_port_bin "${CMAKE_CURRENT_BINARY_DIR}/port-tests")

# The QEMU binary, from the configuration's xpm .bin (the per-config pin).
unset (_qemu CACHE)
find_program (
  _qemu qemu-system-riscv32
  HINTS "${CMAKE_BINARY_DIR}/xpacks/.bin"
)
if (NOT _qemu)
  set (_qemu "qemu-system-riscv32")
endif ()

include ("${CMAKE_SOURCE_DIR}/cmake/uos-tests-run.cmake")

# -----------------------------------------------------------------------------
# One CTest case per test the builder built.
foreach (_app IN LISTS UOS_TESTS_APPS)
  if (TARGET "${_app}-qemu")
    add_test (
      NAME "${PLATFORM_NAME}-${_app}-qemu"
      COMMAND
        "${_qemu}" -M virt
        -cpu rv32,f=false,d=false,zfa=false,zba=true,zbb=true,zbs=true,zbkb=true,zcb=true,zcmp=true
        -smp 1 -m 128M -nographic -bios none
        -kernel "${_port_bin}/test/${_app}-qemu.elf"
        -semihosting-config enable=on,target=native
        -monitor none -serial stdio
    )
    uos_tests_timeout ("${_app}" _timeout)
    set_tests_properties (
      "${PLATFORM_NAME}-${_app}-qemu"
      PROPERTIES LABELS "qemu" TIMEOUT ${_timeout}
                 FAIL_REGULAR_EXPRESSION "RESULT: FAIL"
                 SKIP_REGULAR_EXPRESSION "RESULT: SKIP"
    )
  endif ()
endforeach ()

# -----------------------------------------------------------------------------
message (STATUS "RISC-V QEMU virt RV32: tests registered (${PLATFORM_NAME})")
