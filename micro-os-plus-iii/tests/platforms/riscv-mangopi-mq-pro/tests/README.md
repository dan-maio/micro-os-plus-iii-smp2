# MangoPi MQ-Pro tests

The portable RISC-V test set, adapted from the Milk-V Duo for the Allwinner
D1 (one T-Head C906, 512 MiB/1 GiB DDR at `0x40000000`). Each directory is
one application with its own `os_main`; the board's `hw.sh` runner loads it
with xfel, and the verdict comes back through the RTC registers. The UART
is never opened by the runner.

## Hardware tests (xfel + RTC verdict)

`clock-catchup`, `clock-test`, `exc-test`, `mmu-test`, `mos-test1`,
`mutex-ceiling-test`, `sc-test-ko`, `smp-mat-test`, `smp_test0`,
`smp-test1`, **`wifi-net`** and **`wifi-ram-disk`** (below).

All twelve pass on the MQ-Pro.

## QEMU tests

The same applications are also built for QEMU's virt machine (the D1 is not
modelled); `semihosting-test` exists only there, because hardware has no
debugger. All eleven QEMU cases pass.

## wifi-net — the Wi-Fi stack on the scheduler

`wifi-net` is the bare-metal RTL8723DS station and lwIP, re-hosted on
µOS++. The driver code is the same one that works without an OS
(`src/wifi/` of the MangoPi project, under
`test/boards/mangopi-mq-pro/wifi/`); what this test adds is the
concurrency: two threads, a shared console, and a poll cadence the radio
can live with.

| thread | priority | stack | period | what it owns |
|---|---|---|---|---|
| `net` | above normal | 32 KiB | 2 ms | SDC1 + the RTL8723DS, the 802.11 state machine, lwIP |
| `sd` | normal | 8 KiB | 2 s | SDC0: card presence only |
| `os_main` | normal | 8 KiB | — | starts both, waits for the verdict |

The rules that make it work:

- **One thread owns the radio.** The driver is a sequence of SDIO
  transactions with no internal locking, and the blocking helpers
  (`wlan::join`, `net::ping`) pump the radio themselves, so the script runs
  on the radio thread rather than in `os_main`.
- **2 ms poll.** A 1500-byte frame at 54 Mbit/s arrives in ~220 µs; 2 ms
  keeps the chip's receive FIFO from wrapping during a burst and services
  lwIP's timers well inside their 250 ms granularity, while the thread
  sleeps 99 % of the time.
- **`net` outranks `sd`.** An SD card identification attempt can spend
  hundreds of milliseconds without a preemption point; the radio must keep
  its cadence through that.
- **SDC0 and SDC1 are separate controllers**, so the existence check cannot
  disturb the radio. The one shared resource is the console, and it has one
  writer: the SD thread records the card facts and the radio thread prints
  them (`[SD] card present: ...`), which is why the card's message never
  interleaves with a beacon log line. The thread prints **nothing** while no
  card is present.
- **`plat_delay_us()` sleeps.** Millisecond waits go through `sysclock`;
  sub-millisecond waits stay busy-waits (SDIO timing).

Build it with a network to join (no credentials live in the repository):

```sh
cmake -S . -B test/build-wifi -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro \
      -DWIFI_TEST_SSID=myssid -DWIFI_TEST_PASS=mypassphrase \
      [-DWIFI_TEST_GATEWAY=192.168.1.1] [-DWIFI_TEST_PEER=192.168.1.73]
cmake --build test/build-wifi --target wifi-net-hwd -j
BUILD=test/build-wifi test/boards/mangopi-mq-pro/hw.sh wifi-net
```

Without `WIFI_TEST_SSID` the test still brings the radio up and scans, then
fails the join step with that reason in the log.

The verdict is PASS when the station joins, DHCP leases an address and an
ICMP echo to the gateway is answered. Measured on the board (<SSID>,
WPA2-PSK with a TKIP group key): radio bring-up ≈ 0.7 s, DHCP lease
`192.168.1.9`, 4/4 echoes at 5–16 ms, `0 crc / 0 decrypt` errors.

### Reading the console without the UART

The runner never opens UART0, so the board keeps its console in a RAM ring
(`include/blackbox.hpp`, at `board::SCRATCH_BASE`) and `hw.sh` reads it back
over xfel after the verdict:

```
---- console log (from RAM, read over xfel) ----
[WLAN] SDIO card 024c:d723, 1 function(s), ... 24000000 Hz 4-bit
[SD] card present: 262144000 blocks (128000 MiB), 24000000 Hz, block addressing
...
[TEST] PASS: joined, leased, and the gateway answered
---- end of log ----
```

The text is NUL-terminated as it is written: the last dirty cache line may
never reach DDR before the reset, but a character and its terminator share the
line, so the dump stops exactly at the end of the log — no cache flush and no
uncached mapping are needed (an uncached alias would break `mmu-test`, which
checks cache coherence between the identity map and an alias).

### Keeping the microSD card inserted

After a reset the BROM boots an inserted card, so `hw.sh` normally insists
the card is out. To run with a card (the SD monitor then has something to
report), put the board in FEL from the card's own image, then use
`NORESET=1`:

```sh
BUILD=test/build-wifi NORESET=1 test/boards/mangopi-mq-pro/hw.sh wifi-net
```

## wifi-ram-disk — files over Wi-Fi into a 512 MiB RAM disk

`wifi-ram-disk` keeps `wifi-net`'s radio and lwIP stack unchanged and adds a
**512 MiB RAM disk** at `0x60000000` (the upper half of the 1 GiB board, above
the image's 512 MiB window) with a FAT32 filesystem, and a TCP file server on
port 5000.  The RAM disk and the server are shared with the VisionFive 2 Lite
tests `vf2-wifi-ram-disk` and `smp-net-ram-disk`
(`test/visionfive2-lite/vf2-wifi-ram-disk/`), and so are the PC tools.

One thread, `net`, owns the radio, lwIP and FatFs: the file server works only
inside lwIP callbacks and an lwIP timer (its UDP beacon,
`MQPRO-RAM-DISK <ip> 5000` on port 5001), so nothing else is needed for safety.

```sh
cmake --build test/build-mqpro --target wifi-ram-disk-hwd -j   # build with WIFI_TEST_SSID/PASS
test/mangopi-mq-pro/wifi-ram-disk/wifi-ram-disk-host.py test.bin photo.png --random 2097152
```

The script runs `hw.sh` (xfel), finds the board by its beacon, sends the
files, lists the RAM disk, reads every file back as `<file>.bak`, compares
SHA-256 and sends the verdict; the board stores it in the RTC and resets to
FEL.  Measured: 1.2–1.8 MiB/s up, 1.5–1.6 MiB/s down, SHA-256 identical,
0 CRC / 0 decrypt errors.  Details: `mangopi-mq-pro.md`, section 9b.

## Build

```sh
cmake -S . -B test/build-mangopi-mq-pro -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake \
      -DBOARD=mangopi-mq-pro
cmake --build test/build-mangopi-mq-pro -j
```

## Run

QEMU:

```sh
(cd test/build-mangopi-mq-pro && ctest -LE hwd)
# or one:
BOARD=mangopi-mq-pro BUILD=test/build-mangopi-mq-pro test/qemu.sh clock-test
```

Hardware, one at a time:

```sh
BUILD=test/build-mangopi-mq-pro \
  test/boards/mangopi-mq-pro/hw.sh smp_test0
```

The board must be in FEL with no microSD card. The runner resets the SoC
before and after the test; the test itself also resets at PASS/FAIL/return/
trap. `hw-all.sh` runs the whole set hands-off, but the normal flow is one
test at a time, with the UART free for your terminal.

## Board-specific adaptations

- `mmu-test`: the unmapped hole/alias VA is `0x20000000` (on the Duo it was
  `0x40000000`, which is DDR on the D1).
- `semihosting-test`: QEMU-only (`BOARD_TEST_QEMU_ONLY`).
- `hw_result.hpp` (in `test/mangopi-mq-pro/include/`) shadows the kernel's:
  on hardware it prints the RESULT line to UART, writes the RTC verdict and
  resets; under QEMU it uses semihosting.
- `smp-mat-test`: N/B are `SMP_MAT_N` / `SMP_MAT_B`; the MQ-Pro build uses
  N=320, B=40 (8 row blocks, single hart) and passes on hardware.
- The six harness-suite directories (`blinky`, `fp-switch`,
  `mutex-stress`, `instrumentation`, `rtos-apis`, `cmsis-os-validator`) are
  built by the kernel harness platform
  `micro-os-plus-iii/tests/platforms/riscv-mangopi-mq-pro/`.

## Harness suites

All six harness suites pass on QEMU and on hardware (xfel only, UART
untouched):

| Suite | QEMU | Hardware |
|---|---|---|
| blinky | PASS | PASS |
| fp-switch | PASS | PASS |
| mutex-stress | PASS | PASS |
| instrumentation | PASS | PASS |
| rtos-apis | PASS | PASS |
| cmsis-os-validator | PASS | PASS |

```sh
cd micro-os-plus-iii/tests
xpm run install --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run prepare --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run build   --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run test    --config riscv-mangopi-mq-pro-cmake-gcc-debug
xpm run test-blinky-hwd --config riscv-mangopi-mq-pro-cmake-gcc-debug
```

## Not ported

`smp-test-nested` (CV1800B PLIC/DW timers/UART4) and the TinyUSB CDC/HID
tests are Milk-V Duo hardware tests; the D1 has different timers/PLIC and
this port has no USB device stack yet.
