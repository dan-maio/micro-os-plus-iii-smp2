# Hardware runs, 2026-11-18 (third round): Luckfox Lyra and Raspberry Pi Zero 2 W (AArch32)

Debug builds. The Lyra on the WCH-Link; the Zero 2 W on the "JTAG probe on
Pico" E46268C3571E0D36 (PROBE=cmsis-dap, pinned with UOS_HW_ADAPTER_SERIAL;
telnet/Tcl ports off). The Zero 2 W started 20 s after the Lyra. Verdicts from
the OpenOCD logs only.

## Lyra: the open tests -- every Lyra test has now passed

| test | result | notes |
|---|---|---|
| smp_test7 | PASS 161 s | 256 KiB sent from here with smp_test6's usb_test_host --big (gadget 2207:0019): "ACK: count=262144, status OK", echo matches; y/n typed by the user |
| smp_test_int4 | PASS 86 s | keyboard over USB, the user's kbd_forward |
| smp_test_int5 | PASS 17 s | after two changes (below); before: TIMEOUT 618 s at 20 beats, again at 5 |

smp_test_int5: two console lines per Cortex-M0 interrupt, each mirrored to
semihosting, dropped the WCH-Link ("CMSIS-DAP command mismatch") and the run
hung to the cap. Now 2 beats (the user's choice) and UOS_CONSOLE_UART_ONLY
(as the Lyra's USB tests), the verdict through semihosting::report_result().

## Zero 2 W (AArch32): 17 of 17 pass

smp_test0..4, smp-num-test, smp-pipeline-test, smp-pro-cons-test,
smp-mat-test, sd_test, smp-mat-sdcard-test, clock-test (portable);
mutex-stress, rtos-apis, cmsis-os-validator (upstream's, from
test::<suite>); wifi-net (98 s); wifi-tcp-server (150 s).

wifi-tcp-server: client wifi-tcp-host.py run here against 192.168.1.65 (the
board's Wi-Fi MAC 88:a2:9e:80:26:77, a Raspberry Pi OUI; it joins about 2 min
after the start), 262144 bytes, CRC-32 ff1d2b6f, echo identical, 11 Mbit/s;
the board stored the stream on its card through posix-io on the SDHOST
controller.

The first Zero 2 W batch failed every test in 0.1 s: "JTAG scan chain
interrogation failed: all ones" (the board was not reachable); after the
user's check, all passed.

usb_test: PASS (102 s) on its pre-rewrite storage (the old FatFs on the
boot card, micro-os-plus::devices). Host side send_file.py send_file.py --get
(2993 bytes, read back byte-for-byte), as in its last pass before the
rewrite (2026-10-24); JTAG 4000 kHz.

The posix-io + Chan FatFs usb_test (step 5c) lost the JTAG link right after
mounting the card, 3 runs in 3 ("Invalid ACK (7)", the runner's "DEBUG LINK
LOST"); the app itself kept running and serving USB. The pre-rewrite build
(wt-smp/, the smp branch) passed on the same board, cables and clock, with
no JTAG error. Same compile defines, SD and USB drivers, memory layout,
runner and OpenOCD config; the difference is the storage layer, cause not
found. usb_test is back on the old set (aarch32 rpi-zero-2w only, where it is
proven) until that is investigated; the old FatFs stays while it needs it.
Its other copies (aarch32 rpi3b, aarch64 rpi3b and rpi-zero-2w) are still the
posix-io version, not yet run on hardware.

Earlier failed attempts, not the board's fault: host_xfer.py's 1 MiB file
(its 115200-baud hexdump takes ~300 s, the host waits 120 s for the reply);
python3 -I (send_file.py cannot import host_xfer); no --terminate (the run
waited for its terminating PING). Files: usb-test.sh, usb-test-pre.sh,
zero2w-usb_test*.txt, zero2w-usb_test-pre*.txt.
