/*
 * clocks.cpp -- CH32V307 clocks: HSE 8 MHz x 18 = 144 MHz.
 *
 * WCH's own sequence (EVT system_ch32v30x.c, SystemInit() and
 * SetSysClockTo144_HSE() for the D8C parts), register for register:
 *
 *   reset the RCC to HSI (SystemInit), then
 *   HSE on, wait HSERDY (at most 0x1000 polls, HSE_STARTUP_TIMEOUT)
 *   HCLK = SYSCLK, PCLK2 = HCLK, PCLK1 = HCLK / 2
 *   PLL source HSE, not divided, x18 (D8C encoding: PLLMUL 0000)
 *   PLL on, wait PLLRDY; SYSCLK = PLL, wait SWS = PLL
 *
 * The CH32V307 has no flash wait-state setting (the RM has no FLASH_ACTLR
 * latency field; WCH's code sets none). When HSE does not start, the clock
 * stays at HSI 8 MHz and board::clock_from_hse() says so.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <ch32v307.hpp>
#include <board.hpp>

namespace
{
  bool g_from_hse = false;
}

namespace clocks
{
  void
  init (void)
  {
    using namespace ch32;

    // SystemInit(): back to HSI, everything else off and cleared.
    reg (RCC_CTLR) |= RCC_HSION;
    reg (RCC_CFGR0) &= 0xF0FF0000u;
    reg (RCC_CTLR) &= 0xFEF6FFFFu;
    reg (RCC_CTLR) &= 0xFFFBFFFFu;
    reg (RCC_CFGR0) &= 0xFF00FFFFu;
    reg (RCC_CTLR) &= 0xEBFFFFFFu; // D8C: PLL2/PLL3 off
    reg (RCC_INTR) = 0x00FF0000u;
    reg (RCC_CFGR2) = 0u;

    // SetSysClockTo144_HSE()
    reg (RCC_CTLR) |= RCC_HSEON;
    std::uint32_t n = 0;
    while (((reg (RCC_CTLR) & RCC_HSERDY) == 0u) && (n != 0x1000u))
      {
        ++n;
      }
    if ((reg (RCC_CTLR) & RCC_HSERDY) == 0u)
      {
        g_from_hse = false;
        return;
      }

    reg (RCC_CFGR0) |= RCC_PPRE1_DIV2; // HPRE and PPRE2 stay /1
    reg (RCC_CFGR0) &= ~RCC_PLL_MASK;
    reg (RCC_CFGR0) |= RCC_PLLSRC_HSE | RCC_PLLMUL18_D8C;

    reg (RCC_CTLR) |= RCC_PLLON;
    while ((reg (RCC_CTLR) & RCC_PLLRDY) == 0u)
      {
      }
    reg (RCC_CFGR0) = (reg (RCC_CFGR0) & ~RCC_SW_MASK) | RCC_SW_PLL;
    while ((reg (RCC_CFGR0) & RCC_SWS_MASK) != RCC_SWS_PLL)
      {
      }
    g_from_hse = true;
  }
} // namespace clocks

bool
board::clock_from_hse (void)
{
  return g_from_hse;
}
