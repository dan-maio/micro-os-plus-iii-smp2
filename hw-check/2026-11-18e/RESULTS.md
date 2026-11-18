# Hardware runs 2026-11-18 (e): Pico 2 RP2350B PSRAM board

cortexm-pico2-rp2350b-psram-cmake-gcc-debug, rebuilt from the current tree.
Probe: the generic CMSIS-DAP c251:f001 ("CMSIS-DAP by ARM", serial
202108120001) that the board's openocd.cfg names.

## 15/15 pass (12 under the runner, 3 by hand from flash)

Pass: exc-test, fp-switch, sc-test-ko, smp-mat-test, smp-test-ko,
smp-test-nested (part 1, pico2-psram-batch-part1.txt); smp-test0..5
(part 2, pico2-psram-batch-part2.txt).

Not run: smp-test-nested-clock, smp-test-nested-clock_200,
smp-test-nested-clock_250. They are interactive: the pot (GPIO40) picks a
clock, the user confirms with 'y' on the UART, and the test reboots the chip
into that clock (clocks::reboot_to_index). Under the runner that reboot
starts the firmware in flash, not the test the runner loaded into RAM, and
the debug link is lost ("cannot read IDR"). They need their image in flash.

## Board recovery

At first every test failed in 1 s: the debug port answered (DPIDR
0x4c013477) but neither core could be examined (the firmware in flash locks
debug out). OpenOCD's rescue reset fixed it, writing nothing to the flash:
`openocd -c "set RESCUE 1" -f openocd.cfg -c init -c shutdown`. The same
after the nested-clock test's reboot.

## The three nested-clock tests, by hand from flash: PASS

Each image programmed into flash with OpenOCD (`program <elf> verify`, then
`reset run`, after a rescue reset) and driven by the user on the UART: the pot
picks the clock, 'y' reboots into it, the RESULT line read on the UART.

| Test | Result |
|---|---|
| smp-test-nested-clock_200 | RESULT: PASS (wrk0=10 wrk1=10 uart=5 t2=15 t3=15), block elimination 8.8 s |
| smp-test-nested-clock_250 | RESULT: PASS (wrk0=53 wrk1=53 uart=35 t2=85 t3=84), 53.7 s; a first try hung, the re-run passed |
| smp-test-nested-clock | RESULT: PASS (wrk0=101 wrk1=101 uart=69 t2=169 t3=170), 104.6 s |

Why not under the runner: attached while the test waits at its 48 MHz 'y'
prompt, SWD works (both cores examine). The link is lost at
clocks::reboot_to_index() (boards/pico2/src/clocks.cpp): a watchdog reset of
everything but the oscillators, with the watchdog's debug-pause bits cleared
so it fires under OpenOCD. Open suspect for the lock-out seen afterwards
(DP answers, neither core examinable until a rescue reset): the function
powers the other core off (PSM FRCE_OFF) before the reset, and that bit may
survive the watchdog reset. Not verified.
