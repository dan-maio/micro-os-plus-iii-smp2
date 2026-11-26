// The harness suite "mutex-stress-ram" on the RISC-V Pico 2: upstream's mutex-stress suite
// (tests/sources/mutex-stress), linked to run from SRAM (linker-ram.ld) and loaded
// over the debugger.
//
// Nothing to compile here: the suite is test::mutex-stress (tests.cmake's
// board_test_libs), on the platform's start-up and the common end of a
// portable test (board-tests/portable-test-end.cpp). The builder makes one
// application per folder that has a source file, and this folder is where
// this variant's name comes from.
