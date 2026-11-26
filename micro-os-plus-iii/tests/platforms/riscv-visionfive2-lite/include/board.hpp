/*
 * board.hpp -- StarFive VisionFive 2 Lite board definitions for the tests.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace board
{
  // The console baud rate: U-Boot's (stdout-path = "serial0:115200n8").
  inline constexpr std::uint32_t UART_BAUD = 115200U;

  // Free DDR a test may use as scratch, above the image's 32 MiB window
  // (linker.ld) and inside the 1 GiB the MMU maps.
  inline constexpr std::uintptr_t SCRATCH_BASE = 0x43000000u;
  inline constexpr std::uintptr_t SCRATCH_SIZE = 0x00800000u;
} // namespace board
