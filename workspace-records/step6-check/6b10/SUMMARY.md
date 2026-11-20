# 6b-10: native board code and board tests into the kernel's tests/

Layout (user, 2026-11-19, "upstream shape"): a platform's board code merges into
tests/platforms/<P>/include/ and src/ (as upstream's platforms keep their own
code); its board-only tests go to tests/platforms/<P>/tests/ (tests.cmake,
include/, src/, one folder per test).

Moved from micro-os-plus-iii-posix-arch:
- test/boards/native/include/{led,smp,timer_arm,uart}.hpp -> platforms/native/include/
- test/boards/native/src/{heap,smp}.cpp -> platforms/native/src/ (the board
  library globs src/*.cpp minus *support.cpp, the test start-up)
- test/native/ (tests.cmake, include/, src/, mutex-ceiling-test,
  smp-mutex-stress, smp-rtos-apis) -> platforms/native/tests/
posix-arch's test/ is now upstream's: trace-stderr/, trace-stdout/ only.

Check:
- snapshot.sh (step5c-check/cleanup/) of all 40 configurations; compare.py
  against step5c-check/cleanup/after with the moved paths mapped, the object
  names reduced to their file name and repeated -I counted once:
  all 40 identical.
- native debug and release rebuilt (old test binaries deleted first):
  20/20 each.
