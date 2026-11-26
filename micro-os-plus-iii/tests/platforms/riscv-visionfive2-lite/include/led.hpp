/*
 * led.hpp -- the VisionFive 2 Lite's user LED.
 *
 *   jh7110-common.dtsi  leds { compatible = "gpio-leds"; led-0 {
 *                       gpios = <&aongpio 3 GPIO_ACTIVE_HIGH>; } }
 *   aongpio: pinctrl@17020000 (starfive,jh7110-aon-pinctrl)
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
