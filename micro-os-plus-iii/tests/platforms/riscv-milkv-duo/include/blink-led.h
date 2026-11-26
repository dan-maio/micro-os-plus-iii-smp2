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
// The blinky suite's LED on the Milk-V Duo: the board's led:: driver
// (the RISC-V port's test/boards/milkv-duo, GPIOC24), or the virtual LED of
// the QEMU virt stand-in -- whichever the image links.
// ----------------------------------------------------------------------------

#ifndef PLATFORM_RISCV_MILKV_DUO_BLINK_LED_H_
#define PLATFORM_RISCV_MILKV_DUO_BLINK_LED_H_

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

#endif /* PLATFORM_RISCV_MILKV_DUO_BLINK_LED_H_ */
