// The VisionFive 2 Lite's start-up for upstream's suites (UOS_TESTS_SUITES).
//
// The tests' builder links this file into the suites only (the platform's
// start-up, board-tests/CMakeLists.txt); the board library leaves
// *support.cpp out. The common end of a portable test
// (board-tests/portable-test-end.cpp) calls uos_platform_prologue() before
// the suite's os_main().
//
// The board has four U74 harts and every image here runs the kernel's SMP
// scheduler on all of them (OS_NCPU 4). The board's own tests start CPUs 1..3
// from their os_main() (tests/src/smp-autostart.cpp); a suite's os_main() is
// upstream's and does not, so they are started here, with the same function:
// it creates idle1..3, releases the harts, checks that each runs on its own,
// and ends the run with RESULT: FAIL if one does not.

#include <vf2-smp-start.hpp>

extern "C" void
uos_platform_prologue (void);

extern "C" void
uos_platform_prologue (void)
{
  vf2_smp_start ();
}
