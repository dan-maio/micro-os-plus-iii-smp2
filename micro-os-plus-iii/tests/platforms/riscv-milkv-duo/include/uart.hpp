/*
 * uart.hpp -- the Milk-V Duo console: UART4 (DesignWare APB 8250).
 *
 *   cv180x_base.dtsi  uart4: serial@041C0000 { compatible = "snps,dw-apb-uart";
 *                     clock-frequency = <25000000>; reg-shift = <2>;
 *                     reg-io-width = <4>; }
 *   pins              header pin 4 = GP2 = UART4_TX, pin 5 = GP3 = UART4_RX,
 *                     muxed by the minimal fip's BL2 (bl2_main.c)
 *
 * Polled, blocking transmit. The caller serialises access (the tests wrap
 * each line in a mutex), so the driver keeps no state of its own.
 *
 * On a hardware image (HW_BUILD + SEMIHOST) every write is ALSO sent over
 * semihosting, because the runner (hw.sh) reads the verdict from OpenOCD's
 * log, which only sees semihosting -- the AArch32 port's uart1.puts() does
 * the same.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

// The board contract (micro-os-plus::test-support, board-contract.cpp).
#define PORT_BANNER_LONG "Milk-V Duo (Sophgo CV1800B, T-Head C906, RV64GC)"
#define PORT_BANNER_SHORT "Milk-V Duo"
#define PORT_BANNER_ISA "RV64GC"
#define PORT_BANNER_CPU "C906"

namespace uart
{
  // Program 8N1 at `baud` from the 25 MHz UART clock. 0 keeps whatever the
  // FSBL set up.
  void
  init (std::uint32_t baud);

  void
  put_char (char c);

  void
  write (const char* s);

  // Exactly n bytes; '\n' is sent as "\r\n".
  void
  write (const char* s, std::size_t n);

  // UART4 alone: no copy to semihosting, even on a hardware image.
  void
  write_uart_only (const char* s);

  // The console as the common end of a portable test names it
  // (tests/board-tests/portable-test-end.cpp, the AArch boards' uart1):
  // uart1.puts_uart() writes to the UART only. The verdict reaches
  // semihosting separately (semihosting::report_result).
  struct console
  {
    void
    puts_uart (const char* s) const
    {
      write_uart_only (s);
    }
  };

  inline constexpr console uart1{};
} // namespace uart
