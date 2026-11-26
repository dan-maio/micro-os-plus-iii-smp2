/*
 * led.cpp -- the Pico 2's LED on GPIO25, through SIO.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <led.hpp>
#include <rp2350.hpp>

using rp2350::reg;

namespace
{
  constexpr std::uint32_t BIT = 1u << rp2350::LED_PIN;
  constexpr std::uint32_t FUNC_SIO = 5;
  constexpr std::uint32_t PADS_ISO = 1u << 8, PADS_OD = 1u << 7;
  constexpr std::uint32_t RESET_IO_BANK0 = 1u << 6, RESET_PADS_BANK0 = 1u << 9;
} // namespace

namespace led
{
  void
  init (void)
  {
    rp2350::unreset_block (RESET_IO_BANK0 | RESET_PADS_BANK0);
    const std::uint32_t pad = rp2350::PADS_BANK0_BASE + 0x04 + rp2350::LED_PIN * 4;
    reg (pad) &= ~(PADS_ISO | PADS_OD);
    reg (rp2350::IO_BANK0_BASE + rp2350::LED_PIN * 8 + 4) = FUNC_SIO;
    reg (rp2350::SIO_GPIO_OUT_CLR) = BIT;
    reg (rp2350::SIO_GPIO_OE_SET) = BIT;
  }

  void
  on (void)
  {
    reg (rp2350::SIO_GPIO_OUT_SET) = BIT;
  }

  void
  off (void)
  {
    reg (rp2350::SIO_GPIO_OUT_CLR) = BIT;
  }

  void
  toggle (void)
  {
    reg (rp2350::SIO_GPIO_OUT_XOR) = BIT;
  }

  bool
  is_on (void)
  {
    return (reg (rp2350::SIO_GPIO_OUT) & BIT) != 0;
  }
} // namespace led
