/*
 * board.cpp -- WCH CH32V307V-EVT-R1 start-up hooks.
 *
 * What runs before µOS++: nothing. The CH32V307 boots from user flash,
 * aliased at 0x00000000, straight into _start (startup.S, the image's
 * first word). startup.S sets the trap vector (direct mode: on the QingKe
 * mtvec MODE0 = 0 is the unified entry, MODE1 = 0 executes the instruction
 * at the base -- the standard direct mode) and leaves the PFIC as reset left
 * it: no interrupt enabled, INTSYSCR = 0 (no nesting, no HPE).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <ch32v307.hpp>
#include <riscv-board.hpp>
#include <uart.hpp>
#include <board.hpp>
#if defined(HW_BUILD) && defined(SEMIHOST)
#include <riscv-hart.hpp>
#include <semihosting.hpp>
#endif

namespace clocks
{
  void init (void);
}

extern "C" void
riscv_board_initialize_early (void)
{
  // Pipeline and instruction prediction: the value WCH's start-up writes
  // (startup_ch32v30x_D8C.S; the manual: "configured with default values
  // in the startup file").
  __asm__ volatile("csrw 0xBC0, %0" ::"r"(0x1Fu));

  clocks::init ();

  // The time base: SysTick free-running up from 0 at HCLK, no compare, no
  // interrupt (STE | STCLK; INIT zeroes the counter).
  ch32::reg (ch32::STK_CTLR) = 0u;
  ch32::reg (ch32::STK_SR) = 0u;
  ch32::reg (ch32::STK_CTLR) = ch32::STK_INIT | ch32::STK_STCLK_HCLK | ch32::STK_STE;
}

extern "C" void
riscv_board_initialize (void)
{
  uart::init (board::UART_BAUD);
}

// The console (trace, stdout): the UART, and on a hardware image under the
// debugger also semihosting, so the run's output reaches the PC without
// reading the UART. Not from a trap: a semihosting call halts the core
// (the trap report writes semihosting itself, src/trap.cpp).
extern "C" void
riscv_board_console_write (const char* s, std::size_t n)
{
  uart::write (s, n);
#if defined(HW_BUILD) && defined(SEMIHOST)
  if (semihosting::attached () && !riscv::in_trap ())
    {
      semihosting::write_buf (s, n);
    }
#endif
}
