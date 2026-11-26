/*
 * led.hpp -- the MangoPi MQ-Pro onboard blue status LED.
 *
 * The LED is wired to PD18 and is active high (DTS:
 * <&pio 3 18 GPIO_ACTIVE_HIGH>).  PD18 is shared with the DSI/LVDS
 * LED_PWM function; this port drives it as a plain GPIO.
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
