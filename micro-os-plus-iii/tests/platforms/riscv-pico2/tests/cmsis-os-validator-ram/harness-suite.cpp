// The harness suite "cmsis-os-validator-ram" on the RISC-V Pico 2: upstream's cmsis-os-validator suite
// (tests/sources/cmsis-os-validator), linked to run from SRAM (linker-ram.ld) and loaded
// over the debugger.
//
// Nothing to compile here: the suite is test::cmsis-os-validator (tests.cmake's
// board_test_libs), on the platform's start-up and the common end of a
// portable test (board-tests/portable-test-end.cpp). The builder makes one
// application per folder that has a source file, and this folder is where
// this variant's name comes from.
