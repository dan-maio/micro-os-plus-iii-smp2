/*
 * hw_result.hpp -- how a MangoPi MQ-Pro test ends a run.
 *
 * This header shadows the kernel's tests/smp-support/include/hw_result.hpp (the
 * board's test include directory comes first).  The portable tests call
 * hw_result::ok()/fail(); the harness suites do the same through
 * harness-suite.hpp.
 *
 *   QEMU_BUILD  semihosting is available: SYS_EXIT stops the emulator with
 *               the test's status, exactly as on the other QEMU ports.
 *   HW_BUILD    there is no debugger on this setup (xfel, not JTAG).  Print
 *               the RESULT line on UART0, wait for the transmitter to drain,
 *               then reset the SoC back into FEL with the Allwinner efex
 *               flag + D1 watchdog, so hw.sh can load the next test without
 *               touching the board.
 *
 * The test usually prints its own detailed "RESULT: ..." line; the generic
 * one below is printed too, so a test that stops through hw_result without
 * printing a verdict still gives the runner one.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#if defined(QEMU_BUILD)

#include <semihosting.hpp>

namespace hw_result
{
  [[noreturn]] inline void
  ok () noexcept
  {
    semihosting::exit_success ();
  }

  [[noreturn]] inline void
  fail () noexcept
  {
    semihosting::exit_failure ();
  }
} // namespace hw_result

#else // hardware

#include <riscv-board.hpp>
#include <uart.hpp>

namespace hw_result
{
  [[noreturn]] inline void
  finish (bool pass) noexcept
  {
    uart::write (pass ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    uart::flush ();
    // Writes the verdict to RTC GP_DATA3/4, then resets to FEL.  The UART is
    // for the user; the runner reads the RTC result over xfel.
    riscv_board_test_finish (pass);
  }

  [[noreturn]] inline void
  ok () noexcept
  {
    finish (true);
  }

  [[noreturn]] inline void
  fail () noexcept
  {
    finish (false);
  }
} // namespace hw_result

#endif
