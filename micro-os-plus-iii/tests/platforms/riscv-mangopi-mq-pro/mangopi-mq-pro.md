# MangoPi MQ-Pro (Allwinner D1) — µOS++ III RISC-V port

**Board:** MangoPi MQ-Pro, Allwinner D1 (sun20i), one T-Head C906 (RV64GC), 512 MiB or 1 GiB DDR3  
**Loader:** xfel over the USB-C **OTG** FEL port; image at `0x40000000`  
**Console:** UART0, PB8 = TX / PB9 = RX, 115200 8N1 — left entirely to the user  
**Port id:** `mangopi-mq-pro` (`-DBOARD=mangopi-mq-pro`)  
**Result:** hardware **10/10 port + 6/6 harness PASS** (xfel only); QEMU **11/11 port + 6/6 harness PASS**

---

## 1. Summary

The D1 C906 is driven in machine mode from DDR. There is no JTAG probe on this
setup, so the port does not use OpenOCD or semihosting on hardware:

- **xfel** initializes DDR, writes the flat image at `0x40000000` and jumps to it.
- The test console is UART0; `hw.sh` never opens or configures the serial port.
- The **verdict** is exchanged through the battery-backed RTC general-purpose
  registers, which survive the reset and are readable over FEL.
- Every test ends by resetting the SoC back to FEL, on PASS, FAIL, a returning
  `os_main`, or a trap.
- QEMU's `virt` machine is used for the emulated images (QEMU does not model the
  D1), exactly as the Milk-V Duo port does.

---

## 2. Hardware facts used

| Item | Value |
|---|---|
| CPU | T-Head C906, RV64GC, mhartid 0 |
| DDR | `0x40000000`; the port maps and uses the first 512 MiB |
| Timebase | 24 MHz, read with `rdtime` (`time` CSR) |
| CLINT | `0x14000000`: MSIP `+0x0`, MTIMECMP0 `+0x4000` |
| CLINT MTIME | **not implemented**; `MTIME_READABLE = false`, time from `rdtime` |
| MTIMECMP access | 32-bit halves (`MTIMECMP_SPLIT32 = true`) |
| PLIC | `0x10000000` |
| UART0 | `0x02500000`; PB8/PB9 mux 6; APB1 24 MHz; divisor 13 → 115384 Bd |
| LED | PD18, active high (onboard blue status LED) |
| MMU | Sv39, 2 MiB leaves; C906 `mxstatus.MAEE` set by the board |
| Reset to FEL | RTC GP_DATA2 `0x5AA5A55A` + watchdog soft reset `0x020500A8 = 0x16AA0001` |

The MMU uses T-Head extended PTE attributes (RAM `C|B|SH`, I/O `SO|SH`), so
`riscv_board_initialize_early()` sets `mxstatus.MAEE` before `mmu::init()`. The
board also enables the C906 I/D caches (MHCR bits 0 and 1).

---

## 3. What was added

```
test/boards/mangopi-mq-pro/
  board.cmake                 board facts discovered by CMake
  linker.ld                   image at 0x40000000, 512 MiB window
  include/board.hpp           UART baud, scratch area, nominal CPU clock
  include/riscv-board.hpp     CLINT/PLIC/timebase/MMU regions/hooks
  include/uart.hpp            UART0 API + test banner strings
  include/led.hpp             PD18 API
  include/smp.hpp             RAM window
  src/board.cpp               MAEE, caches, UART init, RTC verdict + reset
  src/uart.cpp                D1 UART0 (clock, pinmux, divisor)
  src/led.cpp                 PD18 GPIO
  hw.sh                       xfel-only one-test runner (no UART access)
  hw-all.sh                   batch runner (still xfel-only)
  qemu.sh                     QEMU-virt runner
  README.md

test/boards/mangopi-mq-pro/
  include/blackbox.hpp        RAM console log, read back over xfel
  src/blackbox.cpp
  wifi/                       the Wi-Fi stack: SDIO, rtw88 port, crypto,
                              lwIP, the D1 SD-card driver (board support)

test/mangopi-mq-pro/
  include/hw_result.hpp       hardware verdict/reset override
  wifi-net/                   the Wi-Fi + lwIP + threads test
  wifi-ram-disk/              files over Wi-Fi into a 512 MiB RAM disk (section 9b)
  include/harness-suite.hpp   harness-suite verdict wrapper
  tests.cmake                 QEMU stand-in, harness suites, QEMU-only test
  clock-catchup/ clock-test/ exc-test/ mmu-test/ mos-test1/
  mutex-ceiling-test/ sc-test-ko/ smp-mat-test/ smp_test0/ smp-test1/
  semihosting-test/           QEMU only (no debugger on the board)
  blinky/ cmsis-os-validator/ fp-switch/ instrumentation/
  mutex-stress/ rtos-apis/    harness-suite placeholders
  README.md
```

Shared port changes, all backward compatible for the other boards:

- `src/semihosting-exit.cpp` — weak `riscv_board_exit(int)` hook in `_Exit()`.
- `src/trap.cpp` — the fault path calls the same weak hook before the
  semihosting report.
- `test/CMakeLists.txt` — new `BOARD_TEST_QEMU_ONLY` list.

The board defines `riscv_board_exit()` and `riscv_board_test_finish()`, which
write the RTC verdict and reset to FEL.

---

## 4. Build

```sh
cmake -S . -B test/build-mangopi-mq-pro -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro
cmake --build test/build-mangopi-mq-pro -j
```

Toolchain: xPack `riscv-none-elf-gcc 15.2.0-1.1`, flags
`-march=rv64imafdc_zicsr_zifencei -mabi=lp64d -mcmodel=medany -mtune=thead-c906`.

---

## 5. QEMU

The D1 is not modelled by QEMU, so the `qemu` image of each test links the
generic `micro-os-plus::riscv-qemu-virt` board (virt CLINT, NS16550 console,
semihosting, its own linker script). The test sources are unchanged.

```sh
(cd test/build-mangopi-mq-pro && ctest -LE hwd)
# one test:
BOARD=mangopi-mq-pro BUILD=test/build-mangopi-mq-pro test/qemu.sh clock-test
```

All eleven QEMU cases pass.

---

## 6. Hardware runner (xfel only)

`hw.sh` never opens, configures or reads the serial port. The test console goes
to UART0 for you to watch with your own terminal.

```sh
BUILD=test/build-mangopi-mq-pro \
  test/boards/mangopi-mq-pro/hw.sh <test>
```

The board must be in FEL mode: unplug the USB-C OTG cable, remove the microSD
card, plug it back in. With no boot media the BROM falls back to FEL **after
each reset**, which is what makes test-after-test runs possible.

Sequence:

```text
xfel reset                         # clean FEL
clear RTC GP_DATA3/4
xfel ddr d1
xfel write 0x40000000 <app>.bin    # objcopy from the ELF
xfel exec 0x40000000
wait for FEL (the test resets itself)
read RTC GP_DATA3/4 over xfel
xfel reset                         # clean FEL for the next test
```

`hw-all.sh` runs the whole set hands-off, but the normal flow is one test at a
time, waiting for a human confirmation between tests.

---

## 7. Verdict and reset protocol

RTC general-purpose registers survive the watchdog reset and are readable over
FEL:

| Register | Address | Value |
|---|---|---|
| GP_DATA2 | `0x07090108` | `0x5AA5A55A` — Allwinner `efex` request |
| GP_DATA3 | `0x0709010C` | `0x4D515052` (`MQPR`) — valid verdict |
| GP_DATA4 | `0x07090110` | `0` PASS, `1` FAIL, `2` SKIP |

Every end path calls `riscv_board_test_finish()`:

- the tests' `hw_result::ok()` / `fail()`;
- `_Exit()` when `os_main` returns, through the weak `riscv_board_exit()` hook;
- the trap handler for an unhandled exception (through the same hook).

`riscv_board_test_finish()` writes the RTC verdict, then
`riscv_board_reset_to_fel()` writes the `efex` magic and the D1 watchdog
soft-reset (`0x020500A8 = 0x16AA0001`; a 1 s interval watchdog is armed as a
fallback). The runner additionally issues an explicit `xfel reset` before and
after each test.

The UART is purely for the user: the hardware verdict does not depend on it.

---

## 8. Results

### Hardware (xfel only) — 12/12 PASS

| # | Test | Result |
|---|---|---|
| 1 | clock-catchup | PASS |
| 2 | clock-test | PASS |
| 3 | exc-test | PASS |
| 4 | mmu-test | PASS |
| 5 | mos-test1 | PASS |
| 6 | mutex-ceiling-test | PASS |
| 7 | sc-test-ko | PASS |
| 8 | smp-mat-test (N=320/B=40) | PASS |
| 9 | smp_test0 | PASS |
| 10 | smp-test1 | PASS |
| 11 | wifi-net | PASS (RTL8723DS: join, DHCP, ICMP) |
| 12 | wifi-ram-disk | PASS (files over Wi-Fi into a 512 MiB RAM disk, SHA-256 identical; section 9b) |

### QEMU virt — 11/11 PASS

The same ten applications plus `semihosting-test`, which is QEMU-only because
the hardware setup has no debugger to service semihosting.

### Harness suites — 6/6 QEMU, 6/6 hardware

Built by the kernel harness platform `tests/platforms/riscv-mangopi-mq-pro`
(see section 10), and run on hardware through the same xfel-only `hw.sh`.

| Suite | QEMU | Hardware (xfel) |
|---|---|---|
| blinky | PASS | PASS |
| fp-switch | PASS | PASS |
| mutex-stress | PASS | PASS (38.6 s) |
| instrumentation | PASS | PASS |
| rtos-apis | PASS | PASS |
| cmsis-os-validator | PASS | PASS |

---

## 9. Test adaptations

- **`mmu-test`**: the unmapped hole and alias VA is `0x20000000`. On the Duo
  the test used `0x40000000`, but on the D1 that address is DDR.
- **`semihosting-test`**: listed in `BOARD_TEST_QEMU_ONLY`; no hardware image.
- **`hw_result.hpp`**: shadows the kernel's header. On `QEMU_BUILD` it uses
  semihosting; on hardware it prints the RESULT line to UART, writes the RTC
  verdict and resets.
- **`smp-mat-test`**: N/B are `SMP_MAT_N` / `SMP_MAT_B`; this board's
  `tests.cmake` builds it with N=320, B=40 (8 row blocks, single hart), and
  the hardware run at that size passed.
- The test sources are the portable RISC-V set of the Milk-V Duo; the board
  banner comes from `test/boards/mangopi-mq-pro/include/uart.hpp`.

---

## 9a. The Wi-Fi test (`wifi-net`)

`wifi-net` runs the bare-metal RTL8723DS station and lwIP on the scheduler.
It is the same driver code as the standalone MangoPi application (kept under
`test/boards/mangopi-mq-pro/wifi/`, board support rather than a test), with
the concurrency added:

| thread | priority | stack | period | role |
|---|---|---|---|---|
| `net` | above normal | 32 KiB | 2 ms | SDC1 + RTL8723DS + lwIP; runs the scripted bring-up, join, DHCP wait and ping |
| `sd` | normal | 8 KiB | 2 s | SDC0 card-presence probe; prints only when a card is present |
| `os_main` | normal | 8 KiB | — | starts both, waits for the verdict |

Design rules that the port's scheduler imposes:

- **One owner per peripheral.** The radio driver has no internal locking and
  its blocking helpers (`wlan::join`, `net::ping`) pump the radio themselves,
  so the whole script runs on the `net` thread; nothing else may call it.
  SDC0 (the card) and SDC1 (the radio) are different controllers, so the SD
  thread is safe alongside.
- **One writer for the console.** The SD thread records the card facts and
  the `net` thread prints them: a second writer interleaved mid-line
  (`[SD] card present: 2621[RTW] firmware ...`) when both printed.
- **The radio outranks the card.** A card identification attempt can run for
  hundreds of milliseconds without a preemption point.
- **`plat_delay_us()` is a scheduling point** for delays ≥ 1 ms
  (`sysclock.sleep_for`), so bring-up and calibration do not block the SD
  thread; sub-millisecond waits remain busy-waits (SDIO timing).

Credentials are build options, never in the tree:

```sh
cmake -S . -B test/build-wifi -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro -DWIFI_TEST_SSID=myssid -DWIFI_TEST_PASS=pass
cmake --build test/build-wifi --target wifi-net-hwd -j
BUILD=test/build-wifi test/boards/mangopi-mq-pro/hw.sh wifi-net
```

Verdict: PASS when the station joins, DHCP leases an address and the gateway
answers an ICMP echo. Measured: 4/4 echo replies at 5–16 ms, 0 CRC and 0
decrypt errors, DHCP lease `192.168.1.9`.

The join log reports how long the PBKDF2 passphrase derivation took (about
80 ms), and nothing more.  It used to print the derived 256-bit PMK in hex as
well; the PMK is the network's long-term secret (it joins the network and
decrypts captured handshakes) and this console is copied to the RAM log that
anyone with xfel can read, so the line was cut back to the timing (2026-11-11).

### The RAM console log

The runner never opens UART0, so every console character is also appended to
a ring in DDR (`include/blackbox.hpp`) and `hw.sh` prints it after the
verdict:

```
---- console log (from RAM, read over xfel) ----
[WLAN] SDIO card 024c:d723, 1 function(s), ... 24000000 Hz 4-bit
[SD] card present: 262144000 blocks (128000 MiB), 24000000 Hz, block addressing
[NET] DHCP lease: address 192.168.1.9 mask 255.255.255.0 gateway 192.168.1.1
[NET] ping: 4 sent, 4 received, 0% loss, rtt min/avg/max = 5.096/7.920/16.392 ms
---- end of log ----
```

The text is NUL-terminated as it is written: the header word can still be in
the D-cache when the test resets, but a character and its terminator share a
cache line, so the dump is exact without a cache flush.

One MMU fact was added for the Wi-Fi stack: the `0x04000000` window (SDC0,
the microSD controller; SDC1, the RTL8723DS; USB0) is now mapped device.

### Keeping the microSD card in

After a reset the BROM boots an inserted card, so `hw.sh` normally wants it
out. With a card in (the SD monitor then has something to say), put the board
in FEL from the card's own image and run with `NORESET=1`.

## 9b. Files over Wi-Fi into a RAM disk (`wifi-ram-disk`)

The PC sends files to the board over Wi-Fi, the board stores them in a
**512 MiB RAM disk** with a FAT32 filesystem, lists it, and sends every file
back; the PC compares.  The same test exists on the VisionFive 2 Lite over its
Wi-Fi (`vf2-wifi-ram-disk`) and its Ethernet (`smp-net-ram-disk`), with the same
RAM disk, the same file server and the same PC tools.  The MQ-Pro has no
Ethernet: Wi-Fi is its only network.

| piece | where | what |
|---|---|---|
| radio, WPA2, lwIP | `test/boards/mangopi-mq-pro/wifi/` | `wifi-net`'s stack, unchanged |
| RAM disk | `test/visionfive2-lite/vf2-wifi-ram-disk/ram-disk.cpp` (shared) | `0x60000000..0x7fffffff`, FatFs drive 0. The image's window is `0x40000000..0x5fffffff` (`linker.ld`, 512 MiB; the console black box is at `0x58000000`), so the disk is the upper half of the **1 GiB** board. A probe writes two patterns 512 MiB apart (and restores them): on a 512 MiB board `0x60000000` would alias the image, and the probe refuses it |
| filesystem | FatFs (`test/visionfive2-lite/emmc-fs-test/fatfs`) | FAT32, 4 KiB clusters, 511.5 MiB usable; created when the memory holds no FAT boot sector |
| file server | `test/visionfive2-lite/vf2-wifi-ram-disk/file-server.cpp` (shared) | TCP **5000**: `ls`, `df`, `put`, `get`, `rm`, `format`, `verdict`; after a `put` the file is re-read through FatFs and that CRC-32 is returned |
| beacon | same file | UDP broadcast to **5001** once a second: `MQPRO-RAM-DISK <ip> 5000` |
| test | `test/mangopi-mq-pro/wifi-ram-disk/main.cpp` | one `net` thread, then the verdict |
| PC side | `test/mangopi-mq-pro/wifi-ram-disk/wifi-ram-disk-host.py` | a wrapper around the shared runner `vf2-wifi-ram-disk-host.py`, with this board's `hw.sh` |

Threads:

| thread | priority | stack | period | role |
|---|---|---|---|---|
| `net` | above normal | 64 KiB | 2 ms | **owns** SDC1 + RTL8723DS, lwIP (`NO_SYS`) and the RAM disk's FatFs: every file operation runs in an lwIP callback on this thread, and the beacon is an lwIP timer, so this one thread is the whole lock |
| `os_main` | normal | — | 100 ms | starts it, waits for the verdict (600 s watchdog), reports |

Run (board in FEL, microSD removed):

```sh
cmake -S . -B test/build-mqpro -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro -DWIFI_TEST_SSID=myssid -DWIFI_TEST_PASS=pass
cmake --build test/build-mqpro --target wifi-ram-disk-hwd -j
test/mangopi-mq-pro/wifi-ram-disk/wifi-ram-disk-host.py \
    test.bin photo.png --random 2097152
```

The script never opens UART0:

1. runs `hw.sh wifi-ram-disk` (xfel: DDR, load at `0x40000000`, exec);
2. finds the board by its beacon, or -- when a host firewall drops the
   broadcast -- by scanning the local /24 for the file protocol on TCP 5000;
3. sends every file and checks the CRC-32 the board read back from the disk;
4. lists the RAM disk and checks every name and size;
5. reads every file back as `<file>.bak` and compares SHA-256;
6. sends `verdict PASS|FAIL <summary>`: the board records it in the RTC and
   resets to FEL; `hw.sh` reads the verdict and prints the RAM console log.

`--no-load`, `--host IP`, `--keep`, `--build DIR` as on the VisionFive 2.

Measured (2026-11-11, <SSID>, WPA2-PSK with a TKIP group key):

| | |
|---|---|
| board on the network | 8.9 s after the start (xfel load, radio up, WPA2 join, DHCP `192.168.1.9`); 5.8 s in a run where the first join was accepted |
| PC → RAM disk | 128 KiB / 857 KiB / 2 MiB at **1237 / 1721 / 1776 KiB/s**; the board wrote them in 92 / 487 / 1140 ms; CRC-32 of each stored file matches |
| RAM disk → PC | **1492 / 1580 / 1630 KiB/s**; every `<file>.bak` SHA-256 identical |
| radio | 3531 frames received, 0 CRC, 0 decrypt errors; 3262 data frames sent, 0 errors |
| verdict | `wifi-ram-disk PASS` (RTC) |

About 1.5–1.8 MiB/s both ways: what one 802.11g link without aggregation, with
every frame through the SDIO PIO FIFO, carries.  Unlike the VisionFive 2's
AIC8800, the first download is not slower than the others.

The first join after a reload is sometimes refused (the access point still
holds the previous run's association for the same MAC); the test tries three
times.

## 10. Harness suites

The six suites (`blinky`, `fp-switch`, `mutex-stress`, `instrumentation`,
`rtos-apis`, `cmsis-os-validator`) are built by the kernel harness. Added:

```text
micro-os-plus-iii/tests/platforms/riscv-mangopi-mq-pro/
  CMakeLists.txt
  cmake/{definitions,platform-library,dependencies-folders}.cmake
  include/cmsis-plus/platform.h
  include/blink-led.h

micro-os-plus-iii/tests/package.json
  riscv-mangopi-mq-pro-cmake-gcc-debug
  riscv-mangopi-mq-pro-cmake-gcc-release
```

The platform honours the port's `BOARD_TEST_QEMU_ONLY` / `BOARD_TEST_HWD_ONLY`
lists, so `semihosting-test` is registered for QEMU only. Hardware runs use the
port's xfel-only `hw.sh` and the RTC verdict; UART is never opened.

```sh
cd micro-os-plus-iii/tests
xpm run install --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run prepare --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run build   --config riscv-mangopi-mq-pro-cmake-gcc-debug

# all QEMU cases (port tests + harness suites)
xpm run test --config riscv-mangopi-mq-pro-cmake-gcc-debug

# one hardware harness suite at a time
xpm run test-blinky-hwd --config riscv-mangopi-mq-pro-cmake-gcc-debug
```

Not ported: `smp-test-nested` (CV1800B PLIC/DW timers/UART4) and the TinyUSB
CDC/HID tests. The D1 has a different timer/PLIC layout and this port has no
USB device stack yet.

---

## 11. Quick reference

```sh
# build
cmake -S . -B test/build-mangopi-mq-pro -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro
cmake --build test/build-mangopi-mq-pro -j

# QEMU, all
(cd test/build-mangopi-mq-pro && ctest -LE hwd)

# hardware, one test (UART untouched)
BUILD=test/build-mangopi-mq-pro test/boards/mangopi-mq-pro/hw.sh smp_test0

# hardware, list
BUILD=test/build-mangopi-mq-pro test/boards/mangopi-mq-pro/hw.sh list

# harness suites (kernel tests/ tree)
cd micro-os-plus-iii/tests
xpm run build --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run test --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run test-blinky-hwd --config riscv-mangopi-mq-pro-cmake-gcc-debug
```
