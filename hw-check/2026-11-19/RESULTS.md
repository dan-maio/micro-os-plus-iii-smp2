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
