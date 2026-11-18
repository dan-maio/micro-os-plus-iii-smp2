# Hardware runs 2026-11-18 (e): Pico 2 RP2350B PSRAM board

cortexm-pico2-rp2350b-psram-cmake-gcc-debug, rebuilt from the current tree.
Probe: the generic CMSIS-DAP c251:f001 ("CMSIS-DAP by ARM", serial
202108120001) that the board's openocd.cfg names.

## 12/15 pass, 3 not run

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
