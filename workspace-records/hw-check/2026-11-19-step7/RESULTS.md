# Step 7 hardware proof: the OpenOCD route from CTest

## Raspberry Pi 4 B (AArch64): 22/22 pass

JTAG probe on Pico E661410403809139, alone; `rpi4b-run.sh` (the 2026-11-19
script with `ctest -L '^hwd$'`: plain `-L hwd` also matches the reset
fixtures' `hwd-reset` label). Every test preceded by its CTest reset fixture
(PM watchdog, 10 s wait): no power cycle in the whole run.

- First attempt: every reset fixture failed in 0.02 s, OpenOCD saying
  "Can't find openocd.cfg". Bug in step 7: the reset's OpenOCD arguments
  reached uos-hwd-reset.cmake with their ';' still escaped, so OpenOCD got
  one argument. Fixed: joined with '|' and split again in the script (the
  offline check had "failed cleanly", for this wrong reason). Run stopped
  before the network and USB tests.
- smp_test0 alone (`rpi4b-smp_test0.txt`): reset 10.3 s, test 12.4 s, PASS.
- The batch (`rpi4b-batch.txt`): 18/19. `smp-list-storage-test`, the first
  test, ended 2.4 s after the resume with no semihosting output (OpenOCD
  exited; no error printed). Rerun alone with -V
  (`rpi4b-smp-list-storage-rerun.txt`): PASS in 13.3 s,
  "RESULT: PASS (96 nodes in 3 regions checked, 10 churn rounds, heap
  restored)". Open: an early end on the first test after the stopped run;
  not seen again.
- eth-net PASS (13.2 s).
- eth-tcp-server PASS (15.6 s); client here against 192.168.1.191, 262144
  bytes, CRC-32 15aff1b5, echo identical, 142 Mbit/s.
- usb_big_test PASS (19.7 s); usb_test_host --big 4194304: "ACK:
  count=4194304, status OK", all bytes match.

Faster than the hw.sh route (e.g. smp_test4 12.5 s).

## Luckfox Lyra B (AArch32): 21/21 pass

WCH-Link 1a86:8011, alone. `lyra-run.sh`: the 18 automatic tests, then the
two keyboard tests driven from here; then smp_test7. Each test preceded by
its reset fixture (RK3506 global software reset, 3 s): no power cycle.

- Batch (`lyra-batch.txt`): 17/18 ending by themselves, durations as with
  hw.sh. smp_test_int2 printed "RESULT: PASS" and "exit(0)", then OpenOCD
  stayed up (ended by hand after 444 s; the hw.sh route had stopped at the
  RESULT line, 123 s, and never needed the exit). Reproduced twice
  (`lyra-smp_test_int2-rerun.txt`).
- Diagnosis (OpenOCD with its Tcl and GDB ports, gdb on each core): all three
  cores examined with semihosting on, still scheduling; core 1 ran the main
  thread inside exit() -> __call_exitprocs -> ~unique_ptr<thread> ->
  ~thread -> thread::kill(0x73fbab0), core 0's idle thread, which is the
  thread running on core 0 and can never stop being current: kill() yields
  for ever. A kernel SMP bug (src/rtos/os-idle.cpp keeps core 0's idle thread
  in a static unique_ptr, as upstream does for its single core); any SMP test
  whose exit() runs off core 0 can hit it. The secondary cores' idle threads
  were never destroyed.
- Fix (kernel, user's choice): under OS_USE_SMP_SCHEDULER, core 0's idle
  thread is not destroyed at exit (allocated, or constructed in place in
  plain storage with OS_EXCLUDE_DYNAMIC_MEMORY_ALLOCATIONS); single-core
  builds keep upstream's code. smp_test_int2 after the fix
  (`lyra-smp_test_int2-fixed.txt`): PASS, 118.4 s, ended by its exit.
- smp_test_int3 PASS (72.6 s; 6 key records sent with pyusb), smp_test_int4
  PASS (81.0 s; caught int3's gadget, sent none; passes on the
  configuration).
- smp_test7 PASS (54.0 s): usb_test_host --big 262144 to gadget 2207:0019,
  "ACK: count=262144, status OK", all bytes match; the user answered the
  board's y/n on its UART.

## Raspberry Pi Pico 2 (RP2350): 21/21 pass

Raspberry Pi Debug Probe E6633861A34C2E38, alone. `pico2-run.sh` (the
2026-11-19 script, `-L '^hwd$'`; the HID step now first waits for the
previous image's cafe:4012 to go, then for the test's to come):
- 19 automatic (`pico2-batch.txt`), 2.4-40 s each: the suites (rtos-apis,
  mutex-stress, cmsis-os-validator, fp-switch), the four -ram tests (SRAM
  load, VTOR/SP/PC from the vector table), clock-test, exc-test, sc-test-ko,
  smp-mat-test, smp-test-ko, smp-test0..5;
- smp-test-usb-cdc-acm PASS (8.2 s): the board's new /dev/ttyACM1
  (Pico2_CDC-ACM, the user's ttyACM0 untouched), one raw open, a line then
  Escape;
- smp-test-usb-hid PASS (6.3 s): pyusb reports on cafe:4012, text then
  Escape.

## Raspberry Pi 4 B again, with the kernel idle fix: 22/22 (after the port fix)

`after-idle-fix/`: same script. 21/22: every test passed (eth-tcp-server on
a rerun: my client took the address from the ARP table while the board was
still bringing the link up and gave up after one 10 s connect; the rerun
retries every 3 s, `rpi4b-eth-tcp-server-rerun.txt`: PASS, echo identical)
but smp-list-storage-test, which failed again as the batch's first test, and
then 1 of 2 runs alone.

The user's UART showed why: `assertion "!interrupts::in_handler_mode ()"
failed`, aarch64 port src/rtos/os-core.cpp:147, kernel_locked(), this_thread
scan3; abort() then exits through semihosting, so OpenOCD ended with no
RESULT line. (The open "in_handler_mode" item; the hw.sh route had passed
this test every time by chance.) Cause: the AArch64 in_handler_mode() reads
the core number, then that core's _in_isr flag; a thread preempted between
the two and moved to another core reads the flag of the core it left, true
if that core is in its handler then. Fix (aarch64 port, os-inlines.h): with
IRQs unmasked it is a thread (the handler runs masked and never unmasks), so
false; with IRQs masked the thread cannot move, so the two reads agree.
`after-isr-fix/`: 6/6 runs of smp-list-storage-test PASS (13.3 s each).
The AArch32 port reads the same two values the same way (open).

## Raspberry Pi Zero 2 W (AArch32): 18/18 pass

"JTAG probe on Pico" E46268C3571E0D36, PROBE=cmsis-dap, both fixes built
in (the kernel's idle thread, the AArch32 in_handler_mode()).
`zero2w-a32-run.sh`; each test preceded by its watchdog reset fixture.
- 16 automatic (`zero2w-a32-batch.txt`), wifi-net included (82 s: joined,
  DHCP, gateway ping); cmsis-os-validator 21 s on the board.
- wifi-tcp-server PASS (86 s): the client, retried every 5 s, connected on
  attempt 9 once the board had joined: 262144 bytes to 192.168.1.65, CRC-32
  ff1d2b6f, echo identical.
- usb_test PASS (28.6 s): send_file.py --get --terminate run here, read-back
  byte for byte, RESULT: PASS.

## Raspberry Pi Zero 2 W (AArch64): 18/18 pass

Same probe, PROBE=cmsis-dap, tests at UOS_HW_ADAPTER_KHZ=10000 (the user's
choice; the reset sessions at the configuration's 4000 kHz), both fixes
built in. `zero2w-a64-run.sh`.
- First try (`zero2w-a64-first-try/`): every reset session saw an empty JTAG
  chain ("IR capture error; saw 0x00") just after the card swap to AArch64;
  no test ran. After the user's fix a plain connection at 10000 kHz found the
  core and examined all four, and a reset passed.
- 16 automatic (`zero2w-a64-batch.txt`), wifi-net included (59 s).
- wifi-tcp-server PASS (55 s): client on attempt 6, 262144 bytes to
  192.168.1.65, CRC-32 ff1d2b6f, echo identical.
- usb_test PASS (28.3 s): send_file.py --get --terminate, read-back byte for
  byte, RESULT: PASS.

## Raspberry Pi Pico (RP2040): 19/20 (1 not run: flashing failed)

Raspberry Pi Debug Probe E6633861A34C2E38, SWD 15000 kHz (default).
`pico-run.sh` (from today's Pico 2 script):
- 18 automatic (`pico-batch.txt`): 17 PASS; cmsis-os-validator not run:
  "CMSIS-DAP command mismatch", "Failed to write ROM batch algorithm to RAM",
  "** Programming Failed **" (a probe glitch while flashing; rerun pending).
- smp-test-usb-cdc-acm PASS (6.5 s), smp-test-usb-hid PASS (5.3 s), driven
  from here.

## Pi-Zero RP2350B (pico2-pizero): 14/14 pass

XV-Link CMSIS-DAP 0416:5951, alone (no rescue reset needed this time).
`pizero-run.sh`: 12 automatic (`pizero-batch.txt`; psram-exec 121 s,
psram-mat-test-250, exc-test, sc-test-ko, smp-mat-test, smp-test-ko,
smp-test0..5); smp-test-usb-cdc-acm PASS (9.2 s, the board's new
/dev/ttyACM1, a line then Escape) and smp-test-usb-hid PASS (8.3 s, pyusb
reports, text then Escape).

## WeAct RP2350B + PSRAM: 12/12 automatic pass (after a test fix)

CMSIS-DAP c251:f001 (alone); the three smp-test-nested-clock* are
interactive (pot + 'y' on the UART, reboot into a clock) and stay by hand.
- Batch (`psram-batch.txt`): 11/12; smp-test5 hung: core 0 locked up after a
  double fault (CFSR 0x00010001: undefined instruction + instruction access
  violation, on valid code in flash, before the scheduler; core 1 never
  launched). Same with the old hw.sh route, with the old os-idle.cpp, and
  after a power cycle. Each failing run left smp-test5 in the flash, so the
  board "did not boot" after the power cycle; smp-test1 was flashed back.
- Cause: this board's smp-test5 set up its own UART on GPIO0/1 (the Pico 2's
  pins; the user saw nothing on the board's UART, GPIO12/13). GPIO0 is this
  board's PSRAM chip select, driven by the QMI that also fetches the code
  from flash: taking it over broke the instruction fetches. Fix: the test
  takes the pins from PICO2_UART_TX_PIN/RX_PIN (cmake/board.cmake, 12/13),
  as the board's own UART driver does. smp-test5 then PASS (11.5 s,
  `psram-smp-test5-uartfix.txt`), printing on the board's UART.

### PSRAM board: the three interactive clock tests (by hand)

Started by CTest through OpenOCD; the user turns the pot and answers 'y' on
the UART; the clock reboot then loses the semihosting link, so the verdict
is read from the user's UART and the session ended by hand.
- smp-test-nested-clock: PASS on the UART (wrk0=102 wrk1=102 uart=69 t2=172
  t3=171).
- After each clock reboot core 1 is held (PSM FRCE_OFF from
  reboot_to_index): the next session's `reset init` fails ("rp2350.cm1 - Not
  halted"; the open "FRCE_OFF" item). The rescue reset (`set RESCUE 1`)
  clears it; done before each of the next runs.
- smp-test-nested-clock_200 at 200 MHz: after its reboot, assertion in
  first_fit_top::do_deallocate (its PSRAM heap corrupt at the first free).
  Cause: boot.S sets the PSRAM (QMI M1) up at the boot clock with CLKDIV 2,
  timed for 75 MHz at 150 MHz; clocks::init()'s clock-from-scratch path
  slowed it only above 200 MHz, so at 176..200 MHz the PSRAM ran at 88..100
  MHz. Fix (cortexm-pico2/src/clocks.cpp, PICO2_CLOCK_FROM_SCRATCH with
  PICO2_PSRAM, 150 < clock <= 200 MHz): CLKDIV raised to keep it <= 75 MHz.
  Rerun at 200 MHz after the fix: OK on the user's UART (the user confirmed).
- smp-test-nested-clock_250 at 250 MHz: PASS on the UART (wrk0=54 wrk1=54
  uart=35 t2=90 t3=86).

## Nucleo-F411RE: 4/4 pass

ST-LINK/V2.1 0483:374b, alone (`nucleo-batch.txt`): mos-test1 (10.7 s),
rtos-apis (43.2 s), mutex-stress (44.2 s), cmsis-os-validator (31.7 s).

## WeAct F411CE: 5/5 pass

WCH-Link 1a86:8011 (the board's openocd.cfg accepts it beside the DAPLink),
alone (`weactf411-batch.txt`): mos-test1 (8.9 s), spi-pipeline (50.3 s),
rtos-apis (42.4 s), mutex-stress (43.7 s), cmsis-os-validator (23.4 s).

## WeAct F412RE: 5/5 pass

ST-LINK/V2.1 0483:374b, alone (`weactf412-batch.txt`): mos-test1 (11.0 s),
uart-test1 (11.0 s), rtos-apis (43.2 s), mutex-stress (44.0 s),
cmsis-os-validator (31.7 s).
