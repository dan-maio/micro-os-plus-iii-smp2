# MangoPi MQ-Pro — µOS++ III RISC-V board support

Allwinner D1 (sun20i), one T-Head C906, RV64GC, 512 MiB or 1 GiB DDR3.
The port runs entirely from DDR in machine mode. There is **no JTAG** on
this setup: xfel initializes DDR, writes the flat image at `0x40000000`
and jumps to it. UART0 is left entirely to the user.

## Facts used

| Item | Value |
|---|---|
| DDR | `0x40000000`, first 512 MiB mapped |
| CLINT | `0x14000000` — MSIP `+0x0`, MTIMECMP0 `+0x4000`, no MTIME register |
| time source | `rdtime` (`time` CSR), 24 MHz |
| PLIC | `0x10000000` |
| UART0 | `0x02500000`; PB8 = TX, PB9 = RX; APB1 24 MHz; divisor 13 → 115384 Bd |
| LED | PD18, active high |
| MMU | Sv39, 2 MiB leaves; C906 `mxstatus.MAEE` set by the board |
| reset | RTC GP_DATA2 `0x5AA5A55A` (efex) + watchdog soft reset `0x16A A0001` |

The C906 CLINT has no readable MTIME (the port's `TIME_CSR = true`,
`MTIME_READABLE = false`, `MTIMECMP_SPLIT32 = true`). The MMU uses the
T-Head extended PTE attributes (RAM `C|B|SH`, I/O `SO|SH`), which is why
`riscv_board_initialize_early()` sets `mxstatus.MAEE` before `mmu::init()`.

## Files

```
board.cmake          board facts discovered by the CMake build
linker.ld            image at 0x40000000, 512 MiB window
include/board.hpp    UART baud, scratch area, nominal CPU clock
include/riscv-board.hpp  CLINT/PLIC/timebase/MMU regions/hooks
include/uart.hpp     UART0 API + test banner strings
include/led.hpp      PD18 API
include/smp.hpp      RAM window
src/board.cpp        MAEE, caches, UART init, RTC verdict + reset
src/uart.cpp         D1 UART0 driver (clock, pinmux, divisor)
src/led.cpp          PD18 GPIO
hw.sh                xfel-only one-test runner (no UART access)
hw-all.sh            batch runner (still xfel-only)
qemu.sh              QEMU-virt runner for the emulated test images
```

## Hardware runner (xfel only)

`hw.sh` never opens or configures the serial port. The test console goes to
UART0 for you to watch with your own terminal.

The verdict is exchanged through the battery-backed RTC registers, which
survive the reset and are readable over FEL:

```
RTC GP_DATA3 (0x0709010c) = 0x4d515052   ("MQPR")
RTC GP_DATA4 (0x07090110) = 0 PASS, 1 FAIL, 2 SKIP
```

Sequence for one test:

```
xfel reset                         # clean FEL
clear GP_DATA3/4
xfel ddr d1
xfel write 0x40000000 <app>.bin
xfel exec 0x40000000
wait for FEL (the test resets itself)
read GP_DATA3/4 over xfel
xfel reset                         # clean FEL for the next test
```

Every end path resets: `hw_result::ok/fail`, a returning `os_main`
(through the port's `_Exit` hook), and a trap (through the port's fault
hook) all call `riscv_board_test_finish()`, which writes the RTC verdict and
watchdog-resets to FEL.

The board must have **no microSD card** while testing: the BROM only falls
back to FEL after the reset when it finds no boot media.

## Build and run

```sh
cmake -S . -B test/build-mangopi-mq-pro -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro
cmake --build test/build-mangopi-mq-pro -j
(cd test/build-mangopi-mq-pro && ctest -LE hwd)   # QEMU virt suite
```

One hardware test at a time (UART untouched):

```sh
BUILD=test/build-mangopi-mq-pro \
  test/boards/mangopi-mq-pro/hw.sh <test>
```

Batch:

```sh
BUILD=test/build-mangopi-mq-pro \
  test/boards/mangopi-mq-pro/hw-all.sh
```

## Test adaptations

- `mmu-test` uses `0x20000000` as its "unmapped hole" and alias VA. On the
  Duo the hole was `0x40000000`, but on the D1 that is DDR.
- `semihosting-test` is **QEMU-only** (`BOARD_TEST_QEMU_ONLY` in
  `test/mangopi-mq-pro/tests.cmake`): this setup has no debugger to service
  semihosting.
- `smp-mat-test` uses N=320, B=40 (`SMP_MAT_N` / `SMP_MAT_B`; 8 row blocks)
  and passes on the single C906 hart.
- The six harness suites (blinky, fp-switch, mutex-stress, instrumentation,
  rtos-apis, cmsis-os-validator) are built by the kernel harness platform
  `micro-os-plus-iii/tests/platforms/riscv-mangopi-mq-pro/` with the
  `riscv-mangopi-mq-pro-cmake-gcc-debug` / `-release` xpm configs. All six
  pass on QEMU and on hardware through the same xfel-only `hw.sh`.
