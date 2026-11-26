/*
 * board.hpp -- MangoPi MQ-Pro board definitions for the tests.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace board
{
  // UART0 on PB8/PB9, APB1 = 24 MHz, divisor 13 (115384 Bd, +0.16 %).
  inline constexpr std::uint32_t UART_BAUD = 115200U;

  // Free DDR a test may use as scratch, above the image and below the heap
  // limit (0x60000000).  Both the 512 MiB and 1 GiB boards have it.
  inline constexpr std::uintptr_t SCRATCH_BASE = 0x58000000u;
  inline constexpr std::uintptr_t SCRATCH_SIZE = 0x00800000u;

  // The D1 boots at 1.008 GHz after the BROM; xfel does not reclock the CPU.
  inline constexpr std::uint32_t CPU_HZ_NOMINAL = 1008000000U;
} // namespace board
