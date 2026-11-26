/*
 * adc.hpp -- RP2350 12-bit SAR ADC (src/adc.cpp), the Cortex-M pico2 board's
 * bsp/adc.hpp. clk_adc must run first (clocks::adc_clk_init()). On the
 * RP2350B (48 GPIOs) channels 0..7 are GPIO40..47 and 8 is the temperature
 * sensor; on the RP2350A channels 0..3 are GPIO26..29 and 4 the sensor.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace adc
{
  // Out of reset, enabled, ready.
  void
  init (void);

  // A GPIO as an analog input (its digital input off).
  void
  gpio_init (unsigned gpio);

  // One 12-bit conversion on `channel`: 0..4095.
  std::uint16_t
  read (unsigned channel);
} // namespace adc
