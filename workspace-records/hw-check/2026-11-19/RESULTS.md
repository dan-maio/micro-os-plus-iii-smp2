# Hardware runs 2026-11-19: after step 6b (board code and tests in the kernel's
# tests/, Cortex-M suites through the builder; kernel 4e4f6750)

Debug builds. Proves the moved runners (6b-13) and the Cortex-M suite
conversion (6b-14) on hardware.

## WeAct F411: 5/5 pass

WCH-Link 1a86:8011. mos-test1, spi-pipeline, rtos-apis (42 s), mutex-stress
(44 s), cmsis-os-validator (23 s): the suites through the builder, ending in
board-tests/cortexm/test-end.cpp with this board's test-end-console.h
(uart::init, \r\n). weactf411-batch.txt.

## Pico 2: 21/21 pass

Raspberry Pi Debug Probe E6633861A34C2E38 (UOS_HW_ADAPTER_SERIAL), in
parallel with the WeAct. pico2-run.sh: 19 automatic (pico2-batch.txt), then
the two USB tests driven from here.
- suites: rtos-apis, mutex-stress, cmsis-os-validator, fp-switch (builder);
- -ram variants: rtos-apis-ram, mutex-stress-ram, cmsis-os-validator-ram (the
  same start-up and suite targets; their runners at the new place, openocd.cfg
  two levels up), smp-mat-test-ram;
- clock-test, exc-test, sc-test-ko, smp-mat-test, smp-test-ko, smp-test0..5;
- smp-test-usb-cdc-acm: the board's new /dev/ttyACM2 (vendor cafe, model
  Pico2_CDC-ACM; the user's ttyACM0/1 untouched), a line then Escape;
- smp-test-usb-hid: pyusb HID OUTPUT reports on cafe:4012 (the only one on the
  bus), text then Escape.

## Nucleo F411: 4/4 pass

ST-LINK/V2.1, in parallel with the Pico. mos-test1, rtos-apis, mutex-stress,
cmsis-os-validator (the suites through the builder, test-end-console.h with
uart::init and \r\n). nucleof411-batch.txt.

The first attempt failed in 1 s per test: the ST-LINK had enumerated only
partly (no virtual COM port; OpenOCD -d3: "claim interface failed"); a replug
fixed it. Found on the way: five Cortex-M OpenOCD configs (nucleof411, pico,
pico2-pizero, pico2-rp2350b-psram, weactf412) left the Tcl/telnet ports
open, so two boards could collide on 6666/4444; now off, as on the others.

## Pico (RP2040): 20/20 pass

Raspberry Pi Debug Probe. 18 automatic (pico-batch.txt), then the two USB
tests (pico-usb.sh): smp-test-usb-cdc-acm (170 s), smp-test-usb-hid (6 s).
- The USB tests' build commands are identical to 2026-11-18's (step6-check
  6b12 vs 6b14, 62 commands each).
- My "USB only" script still contained the full batch line (a sed that did
  not match): three reruns ran all 18 tests again first; the board was reset
  over and over and its USB then failed to enumerate (kernel: "not accepting
  address, error -71"). Fixed; only the two USB tests ran in the end, the
  gadget enumerated at once.
- The CDC test then got empty lines: the board takes input only while the
  host holds its port open (DTR=1), and `printf > port` opens and closes it
  per write. The scripts now open the board's port once, raw, and send the
  line and Escape over it.

Use /dev/serial/by-id/ names for tio: the ttyACM numbers change with every
replug.

## WeAct F412: 5/5 pass

ST-LINK/V2.1 (DBGMCU IDCODE 0x30006441, STM32F412, checked). mos-test1,
uart-test1, rtos-apis (42 s), mutex-stress (44 s), cmsis-os-validator (32 s):
the suites end through its test-end-console.h (uart::init, then 20 ms for the
DMA). Its console is USART1 (PA9/PA10), not the ST-LINK's virtual COM port.
weactf412-batch.txt.

## PSRAM board (WeAct RP2350B): fp-switch pass

CMSIS-DAP c251:f001, after a rescue reset; only fp-switch (the one test of
this board that 6b-14 changed: it came from the Pico 2's suite wrapper, now
from upstream's suite through the builder), 13 s. psram-fp-switch.txt.

## Cortex-M after step 6b: all proven

WeAct F411 5/5, Pico 2 21/21, Nucleo F411 4/4, Pico 20/20, WeAct F412 5/5,
PSRAM fp-switch. Not run: the Pico 2 Pi Zero (no suites; only its runner
path moved in 6b-13).

## Pico 2 Pi Zero: 12/12 pass

XV-Link CMSIS-DAP 0416:5951, after a rescue reset; the 12 automatic tests
(its runner path moved in 6b-13; the two USB tests passed 2026-11-18 and are
unchanged). pico2-pizero-batch.txt.

## Luckfox Lyra: 18/18 pass (the automatic ones)

WCH-Link 1a86:8011. lyra-batch.txt: 14/18 at first; the 4 failures
(smp_test_int2, smp_test0, smp_test1, smp_test3) were all debug-link losses
("DEBUG LINK LOST", "Failed to write memory", "CMSIS-DAP command mismatch"),
all inside the window in which the Pi Zero's CMSIS-DAP session ran beside
it. Rerun alone (lyra-rerun4.txt): 4/4. Not run: smp_test7 (USB transfer from
the PC plus the user's y/n), smp_test_int3 and smp_test_int4 (keyboard over
USB); they passed 2026-11-18 and their code did not change in 6b.
Lesson: do not run Lyra beside another CMSIS-DAP board (the Pico 2 + WeAct
F411 pair was fine).

## Luckfox Lyra: the three USB tests -- 21/21 in all

lyra-usb.sh / lyra-kbd.sh, driven from here:
- smp_test7 PASS (60 s): 262144 bytes to gadget 2207:0019 with smp_test6's
  usb_test_host --big ("ACK: count=262144, status OK", all bytes match); the
  user answered the board's y/n on its UART.
- smp_test_int3 PASS (77 s) and smp_test_int4 PASS (86 s): the verdict is the
  host configuring the OTG1 gadget 2207:001B, which needs the Lyra's USB1 port
  connected (it was not at first: int4 failed "NOT configured within 120 s";
  int3's first try died loading while the Pi 4 B's probe was being plugged
  in). Key records ('K', value, code LE16, seq LE32 on bulk EP 0x02) sent with
  pyusb instead of the root-only kbd_forward: 6 for int3; for int4 the script
  caught int3's gadget still on the bus and sent none (the test passes on
  the configuration alone).

## Raspberry Pi 4 B (AArch64): 22/22 pass

JTAG probe on Pico E661410403809139, alone (rpi4b-run.sh, its runner and
OpenOCD config at their new place, tests/platforms/aarch64-rpi4b/):
- 19 automatic (rpi4b-batch.txt), the upstream suites included;
- eth-net PASS (25 s);
- eth-tcp-server PASS (28 s): client eth-tcp-host.py run here against
  192.168.1.191 (MAC dc:a6:32:65:cc:bb), 262144 bytes, CRC-32 15aff1b5, echo
  identical, 165 Mbit/s;
- usb_big_test PASS (31 s): usb_test_host --big 4194304 run here, "ACK:
  count=4194304, status OK", all bytes match.

## After step 6b, proven on hardware today

Cortex-M: WeAct F411 5/5, Pico 2 21/21, Nucleo F411 4/4, Pico 20/20, WeAct
F412 5/5, PSRAM fp-switch, Pico 2 Pi Zero 12/12. AArch32: Luckfox Lyra 21/21.
AArch64: Pi 4 B 22/22. Not run today: the Pi 3 B and Zero 2 W (AArch32,
AArch64), whose runners moved the same way as the Pi 4 B's.
