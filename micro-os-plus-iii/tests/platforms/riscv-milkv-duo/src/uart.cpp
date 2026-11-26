/*
 * uart.cpp -- Milk-V Duo console on UART4 (DesignWare APB 8250, 32-bit
 * registers, reg-shift 2).
 *
 * The divisor is NOT reprogrammed. The minimal fip's BL2 set UART4 to
 * 115200 8N1 assuming a 25 MHz clock (bl2_main.c), and that is what the
 * terminal is known to work with. The SDK's clock tree gives clk_uart4 a
 * parent of clk_cam0_200 (clk-cv180x.c), so the 25 MHz is an assumption this
 * driver does not repeat: it keeps the working divisor and only makes sure
 * the line format and FIFOs are as expected.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstddef>
#include <cstdint>

#include <uart.hpp>

#if defined(HW_BUILD) && defined(SEMIHOST)
#include <semihosting.hpp>
#include <riscv-hart.hpp>
#endif

namespace
{
  constexpr std::uintptr_t UART4_BASE = 0x041C0000u;

  constexpr unsigned THR = 0x00; // transmit holding (DLAB=0)
  constexpr unsigned FCR = 0x08; // FIFO control
  constexpr unsigned LCR = 0x0C; // line control
  constexpr unsigned LSR = 0x14; // line status

  constexpr std::uint32_t LSR_THRE = 1u << 5;

  inline volatile std::uint32_t&
  reg (unsigned off)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (UART4_BASE + off);
  }

  void
  raw_put (char c)
  {
    while ((reg (LSR) & LSR_THRE) == 0)
      {
      }
    reg (THR) = static_cast<std::uint8_t> (c);
  }

  void
  raw_write (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            raw_put ('\r');
          }
        raw_put (s[i]);
      }
  }

#if defined(HW_BUILD) && defined(SEMIHOST)
  // Copy to semihosting in NUL-terminated chunks: one trap per chunk, not
  // one per character (each trap is a JTAG round trip).
  // Not from a trap: each semihosting call halts the hart for a JTAG round
  // trip (tens of ms), far longer than a tick. Output written by a handler
  // (a timer callback's printf) goes to the UART only. Verdicts and exits
  // are written from threads.
  void
  semi_copy (const char* s, std::size_t n)
  {
    if (!semihosting::attached () || riscv::in_trap ())
      {
        return;
      }
    char chunk[128];
    std::size_t i = 0;
    while (i < n)
      {
        std::size_t k = 0;
        while (i < n && k < sizeof (chunk) - 1)
          {
            chunk[k++] = s[i++];
          }
        chunk[k] = '\0';
        semihosting::write_str (chunk);
      }
  }
#endif
} // namespace

namespace uart
{
  void
  init (std::uint32_t baud)
  {
    (void) baud; // the BL2 divisor is kept, see above
    reg (LCR) = 0x03; // 8N1, DLAB=0
    reg (FCR) = 0x07; // FIFOs on, both cleared
  }

  void
  put_char (char c)
  {
    raw_put (c);
  }

  void
  write (const char* s)
  {
    std::size_t n = 0;
    while (s[n] != '\0')
      {
        ++n;
      }
    write (s, n);
  }

  void
  write (const char* s, std::size_t n)
  {
    raw_write (s, n);
#if defined(HW_BUILD) && defined(SEMIHOST)
    semi_copy (s, n);
#endif
  }

  void
  write_uart_only (const char* s)
  {
    std::size_t n = 0;
    while (s[n] != '\0')
      {
        ++n;
      }
    raw_write (s, n);
  }
} // namespace uart

extern "C" void
riscv_board_console_write (const char* s, std::size_t n)
{
  uart::write (s, n);
}
