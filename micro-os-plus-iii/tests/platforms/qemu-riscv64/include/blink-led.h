/*
 * This file is part of the µOS++ project (https://micro-os-plus.github.io/).
 * Copyright (c) 2026 Liviu Ionescu. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose is hereby granted, under the terms of the MIT license.
 *
 * If a copy of the license was not distributed with this file, it can be
 * obtained from https://opensource.org/licenses/mit.
 */

// ----------------------------------------------------------------------------
// The blinky suite's LED: the virtual LED of the port's QEMU virt board.
// ----------------------------------------------------------------------------

#ifndef PLATFORM_QEMU_RISCV64_BLINK_LED_H_
#define PLATFORM_QEMU_RISCV64_BLINK_LED_H_

#include <led.hpp>

class blink_led
{
public:
  blink_led ()
  {
    // No hardware changes on constructors.
  }

  ~blink_led ()
  {
    // No hardware changes on destructors.
  }

  void
  power_up ()
  {
    led::init ();
    led::on ();
  }

  void
  blink ()
  {
    led::toggle ();
  }

  void
  power_down ()
  {
    led::off ();
  }
};

#endif /* PLATFORM_QEMU_RISCV64_BLINK_LED_H_ */
