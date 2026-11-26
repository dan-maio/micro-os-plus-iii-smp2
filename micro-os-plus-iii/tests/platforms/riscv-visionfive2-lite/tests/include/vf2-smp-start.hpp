/*
 * vf2-smp-start.hpp -- start CPUs 1..3 of the VisionFive 2 Lite from a test
 * (src/smp-autostart.cpp).
 *
 * Called on CPU 0, from os_main() context, with the scheduler running.
 * Creates idle1..3, releases CPUs 1..3, checks that each runs its idle
 * thread on its own hart and prints one `smp: ...` line. Returns true when
 * every core runs; when one does not, it prints a RESULT: FAIL line and ends
 * the run (hw_result::fail()).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

bool
vf2_smp_start (void);
