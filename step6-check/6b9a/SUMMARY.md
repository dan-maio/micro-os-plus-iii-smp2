# 6b-9a: upstream's suites from test::<suite> on the Pi platforms

mutex-stress, rtos-apis, cmsis-os-validator: UOS_TESTS_SUITES on the five
Pi platforms; the builder links test::<suite> (+ xpacks::chan-fatfs and
iii-posix-io for rtos-apis, xpacks::arm-cmsis-os-validator for the
validator) on the platform's start-up. The 15 per-board wrapper folders
and the ports' harness targets are gone; the validator's glue is in
platforms/<P>/src/cmsis-os-validator-support.cpp (uos_test_prologue()
pins the main thread to core 0).

Check (run.sh, check.py -> check.txt): all 40 configurations against 6b-8:
only the suites' and portable tests' commands changed; ctest -N identical.
All 14 built; every suite and portable test passed (run.txt): 23/23 on
each of the 10 Pi configurations, 19/19 native x2; suite hardware images
built (3 per Pi configuration). The validator's interrupt cases
(TC_*Interrupts, messages and mail from an ISR) pass with the moved glue.

Commits (align/smp-upstream, pushed to local): micro-os-plus-iii
17d70811, aarch32 4bbd2a9, aarch64 56fa7f6.
