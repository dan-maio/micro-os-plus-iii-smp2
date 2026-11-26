/*
 * led.hpp -- the Milk-V Duo's blue user LED.
 *
 * GPIOC (gpio2, 0x03022000, DesignWare APB GPIO) bit 24. The SDK's
 * device/generic/rootfs_overlay/duo/mnt/system/blink.sh drives Linux GPIO
 * 440 = gpio2 base 416 + 24; the cv1800 Eclipse project drives the same pin.
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
