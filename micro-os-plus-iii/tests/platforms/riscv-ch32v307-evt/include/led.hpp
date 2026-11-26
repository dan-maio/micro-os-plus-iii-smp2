/*
 * led.hpp -- LED1 of the CH32V307V-EVT, wired by jumper to PD0, active low
 * (the EVT's LEDs are not connected to the SoC in its layout).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

namespace led
{
  void
  init (void);

  void
  on (void);

  void
  off (void);

  void
  toggle (void);

  bool
  is_on (void);
} // namespace led
