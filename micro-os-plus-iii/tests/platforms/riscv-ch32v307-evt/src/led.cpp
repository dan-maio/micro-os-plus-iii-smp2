/*
 * led.cpp -- LED1 on PD0, active low: push-pull output, BCR drives it low
 * (on), BSHR high (off).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <ch32v307.hpp>
#include <led.hpp>

namespace
{
  constexpr std::uint32_t kPin = 1u << 0;
}

namespace led
{
  void
  init (void)
  {
    using namespace ch32;
    reg (RCC_APB2PCENR) |= RCC_IOPDEN;
    reg (GPIOD + GPIO_BSHR) = kPin; // off before it becomes an output
    std::uint32_t l = reg (GPIOD + GPIO_CFGLR);
    l = (l & ~0xFu) | GPIO_OUT_PP_2MHZ;
    reg (GPIOD + GPIO_CFGLR) = l;
  }

  void
  on (void)
  {
    ch32::reg (ch32::GPIOD + ch32::GPIO_BCR) = kPin;
  }

  void
  off (void)
  {
    ch32::reg (ch32::GPIOD + ch32::GPIO_BSHR) = kPin;
  }

  bool
  is_on (void)
  {
    return (ch32::reg (ch32::GPIOD + ch32::GPIO_OUTDR) & kPin) == 0u;
  }

  void
  toggle (void)
  {
    if (is_on ())
      {
        off ();
      }
    else
      {
        on ();
      }
  }
} // namespace led
