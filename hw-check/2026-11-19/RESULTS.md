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
