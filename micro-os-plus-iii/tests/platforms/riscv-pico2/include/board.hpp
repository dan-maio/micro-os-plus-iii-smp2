/*
 * board.hpp -- Raspberry Pi Pico 2 (RP2350, Hazard3) board facts for tests.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace board
{
  inline constexpr std::uint32_t UART_BAUD = 115200u;
  inline constexpr std::uint32_t CPU_HZ_NOMINAL = 150000000u;
} // namespace board
