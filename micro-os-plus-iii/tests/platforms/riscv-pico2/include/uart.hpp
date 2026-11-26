/*
 * uart.hpp -- the Pico 2 console: UART0 (PL011) on GPIO0 (TX) / GPIO1 (RX),
 * 115200 8N1 -- the Raspberry Pi Debug Probe's UART pins.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#define PORT_BANNER_LONG "Raspberry Pi Pico 2 (RP2350, 2x Hazard3 RV32)"
#define PORT_BANNER_SHORT "Pico 2"
#define PORT_BANNER_ISA "RV32IMAC"
#define PORT_BANNER_CPU "Hazard3"

namespace uart
{
  void
  init (std::uint32_t baud);

  void
  put_char (char c);

  void
  write (const char* s);

  void
  write (const char* s, std::size_t n);

  // One received byte, or -1 when the RX FIFO is empty (never blocks).
  int
  get_char (void);

  // The UART alone, '\n' sent as "\r\n": no copy to semihosting, even on a
  // hardware image.
  inline void
  write_uart_only (const char* s)
  {
    for (; *s != '\0'; ++s)
      {
        if (*s == '\n')
          {
            put_char ('\r');
          }
        put_char (*s);
      }
  }

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
