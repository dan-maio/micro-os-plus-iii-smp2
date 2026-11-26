/*
 * wifi-platform.hpp -- what the Wi-Fi stack asks of the test it runs in:
 * time, a sleeping delay and one console writer.  Implemented in
 * net/wifi-lwip.cpp on top of the µOS++ system clock and UART0.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

extern "C"
{
  /// printf to the console, one writer at a time (also lwIP's diagnostics).
  void kprintf (const char* format, ...)
      __attribute__ ((format (printf, 1, 2)));
}

namespace wifi_platform
{
  /// Milliseconds since the scheduler started (the system clock's tick).
  [[nodiscard]] std::uint32_t
  millis (void);

  /// Sleep on the system clock: the calling thread leaves the CPU.
  void
  sleep_ms (std::uint32_t ms);
} // namespace wifi_platform
