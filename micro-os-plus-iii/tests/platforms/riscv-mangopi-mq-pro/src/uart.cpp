/*
 * uart.cpp -- MangoPi MQ-Pro console on Allwinner D1 UART0.
 *
 *   UART0      0x02500000
 *   pins       PB8 = TX, PB9 = RX (mux 6 in PB_CFG1, GPIO + 0x34)
 *   clock      APB1 = HOSC / 1 = 24 MHz (CCU + 0x524)
 *   gate/reset UART_BGR (CCU + 0x90C), bit 0 + bit 16
 *   divisor    24,000,000 / (16 * 115,200) = 13.02 -> 13 (115384 Bd)
 *
 * The D1 UART's baud clock is APB1, not a dedicated UART PLL, and it has an
 * integer divisor only.  Everything is programmed here: unlike the Duo, the
 * loader (xfel) does not leave a configured console behind.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstddef>
#include <cstdint>

#include <blackbox.hpp>
#include <uart.hpp>

namespace
{
  constexpr std::uintptr_t GPIO_BASE = 0x02000000u;
  constexpr std::uintptr_t PB_CFG1 = GPIO_BASE + 0x34u;
  constexpr std::uintptr_t PB_PULL1 = GPIO_BASE + 0x58u;
  constexpr std::uint32_t PB8_UART0_TX = 6u;
  constexpr std::uint32_t PB9_UART0_RX = 6u;

  constexpr std::uintptr_t CCU_BASE = 0x02001000u;
  constexpr std::uintptr_t CCU_APB1_CLK = CCU_BASE + 0x524u;
  constexpr std::uintptr_t CCU_UART_BGR = CCU_BASE + 0x90Cu;

  constexpr std::uintptr_t UART0_BASE = 0x02500000u;
  constexpr unsigned THR = 0x00;
  constexpr unsigned IER = 0x04;
  constexpr unsigned DLL = 0x00;
  constexpr unsigned DLH = 0x04;
  constexpr unsigned FCR = 0x08;
  constexpr unsigned LCR = 0x0C;
  constexpr unsigned LSR = 0x14;

  constexpr std::uint32_t LSR_THRE = 1u << 5;
  constexpr std::uint32_t LSR_TEMT = 1u << 6;

  constexpr std::uint32_t APB1_HZ = 24000000u;

  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  void
  delay (unsigned n)
  {
    for (volatile unsigned i = 0; i < n; ++i)
      {
        __asm__ volatile("nop");
      }
  }

  void
  raw_put (char c)
  {
    for (;;)
      {
        if ((reg (UART0_BASE + LSR) & LSR_THRE) != 0)
          {
            break;
          }
      }
    reg (UART0_BASE + THR) = static_cast<std::uint8_t> (c);
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
} // namespace

namespace uart
{
  void
  init (std::uint32_t baud)
  {
    if (baud == 0u)
      {
        baud = 115200u;
      }
    std::uint32_t div = APB1_HZ / (16u * baud);
    if (div == 0u)
      {
        div = 1u;
      }

    // APB1 = HOSC / 1 = 24 MHz (M = 0, N = 0, source = HOSC).
    reg (CCU_APB1_CLK) = 0;

    // PB8/PB9 -> UART0 (mux 6), pull-up on RX.
    std::uint32_t v = reg (PB_CFG1);
    v = (v & ~0xFFu) | (PB8_UART0_TX << 0) | (PB9_UART0_RX << 4);
    reg (PB_CFG1) = v;

    v = reg (PB_PULL1);
    v = (v & ~0x0Fu) | 0x05u; // pull-up on PB8 and PB9
    reg (PB_PULL1) = v;

    // Pass the bus clock and de-assert the UART0 reset.
    reg (CCU_UART_BGR) = (1u << 0) | (1u << 16);
    delay (10000);

    reg (UART0_BASE + IER) = 0;
    reg (UART0_BASE + LCR) = 0x80; // DLAB
    reg (UART0_BASE + DLL) = div & 0xFFu;
    reg (UART0_BASE + DLH) = (div >> 8) & 0xFFu;
    reg (UART0_BASE + LCR) = 0x03; // 8N1
    reg (UART0_BASE + FCR) = 0x07; // FIFOs on, both cleared
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
  }

  void
  flush (void)
  {
    // Bounded wait for the FIFO and shift register to empty.
    for (unsigned i = 0; i < 20000000u; ++i)
      {
        if ((reg (UART0_BASE + LSR) & LSR_TEMT) != 0)
          {
            break;
          }
      }
    delay (1000);
  }
} // namespace uart

extern "C" void
riscv_board_console_write (const char* s, std::size_t n)
{
  for (std::size_t i = 0; i < n; ++i)
    {
      blackbox::put (s[i]);
    }
  uart::write (s, n);
}
