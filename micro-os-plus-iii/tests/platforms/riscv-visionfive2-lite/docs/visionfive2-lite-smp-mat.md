# VisionFive 2 Lite — smp-mat-test N=320/B=40 and the UART/semihosting cost

**Board:** StarFive VisionFive 2 Lite (JH-7110S, 4 × SiFive U74, RV64GC, 8 GB)  
**Loader/console:** U-Boot + OpenOCD over JTAG; UART0 console at 115200  
**Test:** `test/visionfive2-lite/smp-mat-test`, `N=320`, `B=40`, `OS_NCPU=4`  
**Result:** **PASS**, 4 s end-to-end (was 49 s), max difference 0, speedup 3.928×

---

## 1. Summary

The parallel block solver is fast:

```
N = 320, B = 40, cores = 4
8 row blocks, two per U74 core
Parallel Block Solver: 36092 us   (36 ms)
Classical Solver:      141780 us (142 ms)
Speedup Ratio:         3.928x
Max Absolute Difference: 0
solver_c0..c3 ran on mhartid 1..4
RESULT: PASS
```

Yet the first hardware run took **49 s**. The solver was not the cost: the
console driver copied **every UART write** to semihosting, and each
semihosting call is a JTAG round trip that halts the hart. The test prints 32
matrix lines, each assembled from many small `write_str` / `write_fmt` /
`print_float` calls, so the run paid hundreds of probe round trips.

After changing the VF2 UART to copy only the verdict line to semihosting, the
same test runs in **4 s**.

---

## 2. The test size

`N/B = 8` row blocks, two per each of the four U74 harts. The VF2 build gets
the size from the port's `tests.cmake`:

```cmake
function (board_test_options _app _out)
  if ("${_app}" STREQUAL "smp-mat-test")
    set (${_out} -DSMP_MAT_N=320 -DSMP_MAT_B=40 PARENT_SCOPE)
  ...
```

and the source keeps compile-time defaults:

```c
#ifndef SMP_MAT_N
#define SMP_MAT_N 240
#endif
#ifndef SMP_MAT_B
#define SMP_MAT_B 30
#endif
#define N SMP_MAT_N
#define B SMP_MAT_B
```

---

## 3. Why the console was slow

`test/boards/visionfive2-lite/src/uart.cpp` wrote the bytes to UART0 and then
mirrored the same buffer to semihosting:

```cpp
void uart::write (const char* s, std::size_t n)
{
  raw_write (s, n);
#if defined(HW_BUILD) && defined(SEMIHOST)
  semi_copy (s, n);      // every write, every line
#endif
}
```

The mirror exists because `hw.sh` reads the RESULT line and the exit code
from OpenOCD's semihosting log. But copying *all* output makes the debug
channel the bottleneck:

- one semihosting `SYS_WRITE` per `uart::write` call,
- each call halts the hart and waits for a JTAG/USB round trip,
- the test produces hundreds of small writes,
- the 4 U74 cores, 8 GB RAM and 1 GHz clock cannot speed up one 115200 UART
  or one JTAG probe; the console is serial and the debug channel is not a
  data path.

The MangoPi MQ-Pro looked much faster because its port writes only to UART0
and gets the verdict from the RTC over xfel — there is no semihosting on the
hardware path at all.

---

## 4. The fix: verdict-only semihosting

The Pico 2 board already used the right pattern. The VF2 now does the same:

```cpp
bool has_verdict (const char* s, std::size_t n)
{
  static constexpr char key[] = "RESULT:";
  ... // substring search
}

void semi_copy (const char* s, std::size_t n)
{
  if (n == 0 || !semihosting::attached () || riscv::in_trap ()
      || !has_verdict (s, n))
    return;
  semihosting::write_buf (s, n);   // SYS_WRITE, one memory read
}
```

- Only a write containing `RESULT:` crosses JTAG.
- All other console output stays on UART0 (visible in the normal console).
- The semihosting `SYS_EXIT` still ends the run, so `hw.sh` still gets the
  exit code and the RESULT line.

This is a pure performance change: the test, the verdict, and the runner
interface are unchanged.

---

## 5. Before and after

| | Before | After |
|---|---|---|
| `hw.sh smp-mat-test` total | **49 s** | **4 s** |
| Parallel solver | 36.092 ms | 36.092 ms (math unchanged) |
| Classical solver | 141.780 ms | 141.780 ms |
| Semihosting traffic | every console write | only `RESULT:` + `SYS_EXIT` |
| Full console | UART + JTAG | UART only |

The remaining 4 s is JTAG load/park plus the test and its UART output, not
the solver.

---

## 6. Reproduce

```sh
# build the VF2 test (N=320, B=40 from tests.cmake)
cmake --build test/build-vf2 --target smp-mat-test-hwd

# run it on the board (U-Boot prompt, probe connected)
BUILD=test/build-vf2 \
  test/boards/visionfive2-lite/hw.sh smp-mat-test 900
```

Expected tail:

```
RESULT: PASS (block == classical, one solver per core, pool on every core)
smp-mat-test             PASS  (exit 0, 4s)
```

---

## 7. Files changed

```
test/boards/visionfive2-lite/src/uart.cpp     verdict-only semihosting
test/visionfive2-lite/smp-mat-test/main.cpp   SMP_MAT_N / SMP_MAT_B
test/visionfive2-lite/tests.cmake             VF2 smp-mat N=320/B=40
```

The same optimization applies to the Milk-V Duo's UART driver
(`test/boards/milkv-duo/src/uart.cpp` still mirrors every write); it has not
been applied there.
