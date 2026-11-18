# Hardware runs 2026-11-18 (g): Pico (RP2040)

cortexm-pico-cmake-gcc-debug, rebuilt from the current tree; Raspberry Pi
Debug Probe 2e8a:000c (the board's openocd.cfg). Script run.sh.

## 20/20 pass

Automatic (pico-batch.txt, 18): cmsis-os-validator (+ -ram), mutex-stress
(+ -ram), rtos-apis (+ -ram), exc-test, fp-switch, sc-test-ko, smp-mat-test
(+ -ram), smp-test-ko, smp-test0..5.

smp-test-usb-cdc-acm (123 s): driven from here through its new /dev/ttyACM
(vendor cafe, model Pico2_CDC-ACM, picked by USB ids); a line, then Escape.
At the user's request the tio on /dev/ttyACM1 was stopped before (the user's
tio sessions on ttyACM0 and ttyACM2 left running).

smp-test-usb-hid (6 s): pyusb HID OUTPUT reports on cafe:4012 EP 0x01 ("hello
from the host", then Escape). Only one cafe:4012 on the bus (the Pico's, new
device 074), and the test prints RESULT: PASS only on the host's Escape.
