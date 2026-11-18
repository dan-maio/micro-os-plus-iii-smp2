# Hardware runs 2026-11-18 (f): Pico 2 Pi Zero (RP2350)

cortexm-pico2-pizero-cmake-gcc-debug, rebuilt from the current tree. Probe:
XV-Link CMSIS-DAP 0416:5951 (the board's openocd.cfg); a rescue reset
(`set RESCUE 1`) first, both cores examined.

## 14/14 pass

Automatic (pico2-pizero-batch.txt): exc-test, psram-exec, psram-mat-test-250,
sc-test-ko, smp-mat-test, smp-test-ko, smp-test0..5.

smp-test-usb-cdc-acm (40 s, cdc-acm.txt): driven from here; the board's CDC
port appeared as a NEW /dev/ttyACM (vid cafe, model Pico2_CDC-ACM, picked by
its USB IDs; the user's UART ports untouched); one line sent, then Escape.

smp-test-usb-hid (34 s, usb-hid.txt): driven from here with pyusb instead of
kbd_forward (which needs root to grab a keyboard): two HID OUTPUT reports in
kbd_forward's format (byte 0 = length, then the characters, padded to 64) on
EP 0x01 of cafe:4012 ("hello from the host", then Escape). The device node is
world-writable, so no root was needed. Note: kbd_forward.c's header comment
still says PID 0x4004; its code uses 0x4012, which is right.
