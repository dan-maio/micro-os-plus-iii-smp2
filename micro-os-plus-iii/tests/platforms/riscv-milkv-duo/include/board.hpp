/*
 * board.hpp -- Milk-V Duo board definitions for the tests.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace board
{
  // The console baud rate (the minimal fip's BL2 sets the same).
  inline constexpr std::uint32_t UART_BAUD = 115200U;

  // Free DDR a test may use as scratch, well above the image and its heap
  // start, below the OpenOCD work area (0x83FF0000).
  inline constexpr std::uintptr_t SCRATCH_BASE = 0x83000000u;
  inline constexpr std::uintptr_t SCRATCH_SIZE = 0x00800000u;

  // The C906 runs at 850 MHz after the minimal fip's sys_pll_init().
  inline constexpr std::uint32_t CPU_HZ_NOMINAL = 850000000U;
} // namespace board
