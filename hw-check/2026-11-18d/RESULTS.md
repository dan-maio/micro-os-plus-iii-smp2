# Hardware runs 2026-11-18 (d): Zero 2 W and Pi 3 B, AArch64

Debug builds, `batch.sh` (both boards in parallel, the Pi 3 B started 20 s
later). Zero 2 W: CMSIS-DAP probe E46268C3571E0D36 (`PROBE=cmsis-dap`);
Pi 3 B: Olimex (`PROBE=olimex`). Before the run both AArch64 OpenOCD configs
got `tcl port disabled` / `telnet port disabled` (the two sessions otherwise
compete for ports 6666/4444) and the CMSIS-DAP one the serial pin.

## Pi 3 B (AArch64): 15/15 pass

mutex-stress, rtos-apis, cmsis-os-validator (upstream's suites), smp_test0-4,
smp-num-test, smp-pipeline-test, smp-pro-cons-test, smp-mat-test, sd_test,
smp-mat-sdcard-test, clock-test. 405 s.

## Zero 2 W (AArch64): 17/18 pass

Pass: wifi-net (84 s), wifi-tcp-server (137 s; client wifi-tcp-host.py run
here against 192.168.1.65, MAC 88:a2:9e:80:26:77, 262144 bytes, CRC-32
ff1d2b6f, echo identical, 10.8 Mbit/s), mutex-stress, rtos-apis,
cmsis-os-validator, smp_test0-4, smp-num-test, smp-pipeline-test,
smp-pro-cons-test, smp-mat-test, sd_test, smp-mat-sdcard-test, clock-test.

Fail: usb_test (80 s). This run used its PRE-REWRITE storage (old FatFs,
"FatFsSink (FAT32 tests/)") and still lost the JTAG link right after the
mount, as the posix-io version did on AArch32; this time OpenOCD also saw
"Debug regions are unpowered, an unexpected reset might have happened" and
the gadget never enumerated. So the storage layer is not the cause (the
AArch32 pre-rewrite passes were luck): the failure is at the USB bring-up
(USB power domain + DWC2 start), most likely the board's supply dipping when
the USB port powers up (the runner's hint: "check the board's supply").
The AArch64 usb_test storage change was undone (not committed); the board
recovered by itself for the rest of the batch.

## usb_test again (Zero 2 W, AArch64): PASS

The posix-io + Chan FatFs version (the rewrite, as committed), 36 s; host
side send_file.py send_file.py --get --terminate (2993 bytes, read back
byte-for-byte). Script usb-test-a64.sh, logs zero2w-a64-usb_test-2*.txt.
So the rewrite works on the board; the JTAG-link loss at the USB bring-up
is intermittent, with either storage.
