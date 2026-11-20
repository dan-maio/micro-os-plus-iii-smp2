# Hardware runs, 2026-11-18: Luckfox Lyra and Pico 2 (debug builds)

Each board's tests one after another, the two boards in parallel (two OpenOCD
sessions: the Pico 2 on the Raspberry Pi Debug Probe E6633861A34C2E38, pinned
with UOS_HW_ADAPTER_SERIAL; the Lyra on the WCH-Link, pinned by vid_pid;
telnet/Tcl ports off in both configs). Verdicts from the OpenOCD semihosting
logs only; the UARTs (ttyACM0, ttyACM1) are the user's. The Lyra runner's
"an OpenOCD process is already running" warning is the other board's session.

## Pico 2: 21 of 21 pass (its board tests; not converted by the alignment yet)

smp-test0..5, clock-test, cmsis-os-validator(-ram), exc-test, fp-switch,
mutex-stress(-ram), rtos-apis(-ram), sc-test-ko, smp-mat-test(-ram),
smp-test-ko, smp-test-usb-cdc-acm, smp-test-usb-hid (interactive: the user ran
its host kbd_forward with root; the batch run without it timed out). An LED
check (smp-test1, GPIO25): seen blinking by the user.

## Lyra: the 12 portable tests (first time on hardware) and the board tests

| test | first batch | after the UART console + LED fix |
|---|---|---|
| smp_test0 | PASS 95 s | PASS 26 s |
| smp_test1 | PASS | PASS 27 s |
| smp_test2 | PASS 75 s | PASS 17 s |
| smp_test3 | PASS 73 s | PASS 27 s |
| smp_test4 | PASS 116 s | PASS 26 s |
| smp-num-test | TIMEOUT 625 s (semihosting console) | PASS 40 s |
| smp-pipeline-test | PASS 299 s | PASS 57 s |
| smp-pro-cons-test | PASS 196 s | PASS 22 s |
| smp-mat-test | TIMEOUT 622 s (semihosting console) | PASS 17 s |
| sd_test | PASS 172 s | PASS 22 s |
| smp-mat-sdcard-test | PASS 456 s | PASS 24 s |
| clock-test | PASS 103 s | PASS 21 s |

Board tests (unchanged): smp_test5 PASS, smp_test6 PASS, smp_test_int PASS,
smp_test_int2 PASS, smp_test_int3 PASS (keyboard via kbd_forward),
mutex-stress-test PASS; smp_test_int5 TIMEOUT 622 s (still receiving the M0's
interrupts, heavy semihosting: the known WCH-Link limit); not run: smp_test7
(file over USB + y/n on the UART), smp_test_int4 (keyboard; the user asked not
to repeat it).

Fixes found by these runs (micro-os-plus-iii):
- the portable tests' console on the Lyra was semihosting (printf -> posix-io
  __posix_write -> trace_write -> the board's uart.puts(), which mirrors to
  semihosting): ~1 s per line over the WCH-Link. UOS_TESTS_STARTUP_DEFINES
  UOS_CONSOLE_UART_ONLY on the Lyra platform keeps it on the UART; the RESULT
  line reaches semihosting through semihosting::report_result()
  (board-tests/portable-test-end.cpp);
- the LED: the old board copies drove it, the portable ones had lost it;
  board-tests/include/blink-led.h (upstream's blink_led interface over the
  board's led.hpp) in smp-num, smp-pipeline, smp-pro-cons, smp-mat,
  smp-mat-sdcard;
- interleaved lines: console helpers flush under their mutex; smp-num-test's
  reader streamed num.txt without the mutex;
- smp-num-test runs 15 s (was 60 s).

QEMU/host after these: native 19/19, aarch64-rpi3b 23/23, aarch32-rpi3b 22/23
(cmsis-os-validator's intermittent failure, 12/12 on repeat:
validator-a32-rpi3b/, BASELINE.md). All 40 configurations: only the portable
tests' commands changed, test lists identical (check/check.txt).
