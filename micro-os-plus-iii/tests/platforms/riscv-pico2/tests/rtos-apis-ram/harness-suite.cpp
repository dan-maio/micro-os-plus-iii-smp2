// The harness suite "rtos-apis-ram" on the RISC-V Pico 2: upstream's rtos-apis suite
// (tests/sources/rtos-apis), linked to run from SRAM (linker-ram.ld) and loaded
// over the debugger.
//
// Nothing to compile here: the suite is test::rtos-apis (tests.cmake's
// board_test_libs), on the platform's start-up and the common end of a
// portable test (board-tests/portable-test-end.cpp). The builder makes one
// application per folder that has a source file, and this folder is where
// this variant's name comes from.
