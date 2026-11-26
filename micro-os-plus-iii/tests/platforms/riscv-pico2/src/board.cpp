/*
 * board.cpp -- Raspberry Pi Pico 2 (RP2350, Hazard3) start-up hooks.
 *
 * What runs before µOS++: the boot ROM finds the image's RISC-V IMAGE_DEF
 * block (image-def.S), switches both cores to Hazard3 and enters _start on
 * core 0 from flash (XIP); core 1 waits in the boot ROM until launched.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <rp2350.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>
#include <board.hpp>

namespace clocks
{
  void init (void);
}

extern "C" void
riscv_board_initialize_early (void)
{
  // No external interrupt is used: keep MEIE off (Hazard3's Xh3irq stays
  // as the boot ROM left it, masked).
  RISCV_CSR_CLEAR (mie, riscv::csr::MIE_MEIE);

  clocks::init ();

  // The RISC-V platform timer: count the TICKS block's 1 us tick
  // (MTIME_CTRL: EN = 1, FULLSPEED = 0). The tick generator itself is set up
  // in clocks::init(). Both cores' comparators to "never" first.
  rp2350::reg (rp2350::SIO_MTIME_CTRL) = 0x1u;
}

extern "C" void
riscv_board_initialize (void)
{
  uart::init (board::UART_BAUD);
}
