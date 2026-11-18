# Hardware runs, 2026-11-18 (second pair): Raspberry Pi 4 B and WeAct F411

Debug builds, each board's tests one after another, the two boards in
parallel: the Pi 4 B on the "JTAG probe on Pico" (CMSIS-DAP,
E661410403809139, pinned with UOS_HW_ADAPTER_SERIAL; watchdog reset before each
test, no power cycle), the WeAct F411 on the WCH-Link (pinned by vid_pid).
telnet/Tcl ports off in both configs. Verdicts from the OpenOCD logs only.

## WeAct F411: 5 of 5 pass (its board tests; not converted by the alignment yet)

cmsis-os-validator 23 s, mos-test1 9 s, mutex-stress 44 s, rtos-apis 43 s,
spi-pipeline 51 s.

## Pi 4 B: 19 of 19 pass (the first hardware run of everything converted)

| test | | time |
|---|---|---|
| smp_test0..4 (portable) | PASS | 14-25 s |
| smp-num-test, smp-pipeline-test, smp-pro-cons-test, smp-mat-test (portable) | PASS | 33, 47, 19, 15 s |
| sd_test, smp-mat-sdcard-test (portable, posix-io + Chan FatFs on the boot card) | PASS | 16, 17 s |
| clock-test (portable, the BCM2711 System Timer) | PASS | 20 s |
| mutex-stress, rtos-apis, cmsis-os-validator (upstream's, from test::<suite>) | PASS | 49, 25, 27 s |
| smp-shapes-test (board test, its SD log through posix-io) | PASS | 50 s |
| smp_mem_test, smp_mem_inter_test (board tests) | PASS | 46, 196 s |
| smp-list-storage-test (board test) | PASS on retry, 25 s | |

smp-list-storage-test failed first with "OPENOCD DIED" right after the resume:
the batch's first test, started at the same moment as the WeAct's OpenOCD
session; alone it passed. Start the second board a few seconds later.

Not run (they need the user): usb_big_test (host tool), eth-net and
eth-tcp-server (Ethernet, a TCP client).
