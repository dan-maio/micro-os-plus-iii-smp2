/*
 * uart.hpp -- the CH32V307V-EVT console: USART1, PA9 (TX) / PA10 (RX),
 * 115200 8N1, through the J2 jumpers to the on-board WCH-LinkE's CDC port.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#define PORT_BANNER_LONG "WCH CH32V307V-EVT-R1 (CH32V307, QingKe V4F RV32)"
#define PORT_BANNER_SHORT "CH32V307-EVT"
#define PORT_BANNER_ISA "RV32IMAFC"
#define PORT_BANNER_CPU "QingKe V4F"

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

  // Wait until the last byte has left the shift register.
  void
  flush (void);

  // One received byte, or -1 when nothing was received (never blocks).
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
