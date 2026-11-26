/*
 * uart.cpp -- USART1 on PA9 (TX, alternate push-pull) / PA10 (RX,
 * floating input), polled. USART1 is on APB2 (PCLK2 = HCLK = 144 MHz):
 * BRR = PCLK2 / baud, i.e. USARTDIV = BRR / 16 (RM 18.10.4); 115200 gives
 * 1250 (0x4E2), 0 % error.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <ch32v307.hpp>
#include <board.hpp>
#include <uart.hpp>

namespace uart
{
  void
  init (std::uint32_t baud)
  {
    using namespace ch32;

    reg (RCC_APB2PCENR) |= RCC_IOPAEN | RCC_USART1EN;

    // PA9: CFGHR bits [7:4]; PA10: CFGHR bits [11:8].
    std::uint32_t h = reg (GPIOA + GPIO_CFGHR);
    h &= ~((0xFu << 4) | (0xFu << 8));
    h |= (GPIO_AF_PP_50MHZ << 4) | (GPIO_IN_FLOATING << 8);
    reg (GPIOA + GPIO_CFGHR) = h;

    const std::uint32_t pclk2
        = board::clock_from_hse () ? board::CPU_HZ_NOMINAL : 8000000u;
    reg (USART1_CTLR1) = 0u;
    reg (USART1_BRR) = (pclk2 + baud / 2u) / baud;
    reg (USART1_CTLR1) = USART_UE | USART_TE | USART_RE;
  }

  void
  put_char (char c)
  {
    using namespace ch32;
    while ((reg (USART1_STATR) & USART_TXE) == 0u)
      {
      }
    reg (USART1_DATAR) = static_cast<std::uint8_t> (c);
  }

  void
  write (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            put_char ('\r');
          }
        put_char (s[i]);
      }
  }

  void
  write (const char* s)
  {
    while (*s != '\0')
      {
        if (*s == '\n')
          {
            put_char ('\r');
          }
        put_char (*s++);
      }
  }

  void
  flush (void)
  {
    using namespace ch32;
    while ((reg (USART1_STATR) & USART_TC) == 0u)
      {
      }
  }

  int
  get_char (void)
  {
    using namespace ch32;
    if ((reg (USART1_STATR) & USART_RXNE) == 0u)
      {
        return -1;
      }
    return static_cast<int> (reg (USART1_DATAR) & 0xFFu);
  }
} // namespace uart
