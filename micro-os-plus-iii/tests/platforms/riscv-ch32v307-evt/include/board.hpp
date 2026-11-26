/*
 * board.hpp -- WCH CH32V307V-EVT-R1 board facts for tests.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace board
{
  inline constexpr std::uint32_t UART_BAUD = 115200u;
  inline constexpr std::uint32_t CPU_HZ_NOMINAL = 144000000u;
  inline constexpr std::uint32_t HSE_HZ = 8000000u;

  // What clocks::init() achieved: true when HSE started and the PLL runs
  // from it (144 MHz); false leaves the reset clock, HSI 8 MHz.
  bool
  clock_from_hse (void);
} // namespace board
