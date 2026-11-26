/*
 * key.cpp -- key S2 on PA1: input with pull-up (CNF 10, OUTDR bit = 1
 * selects up), read low when pressed.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <ch32v307.hpp>
#include <key.hpp>

namespace
{
  constexpr std::uint32_t kPin = 1u << 1;
}

namespace key
{
  void
  init (void)
  {
    using namespace ch32;
    reg (RCC_APB2PCENR) |= RCC_IOPAEN;
    std::uint32_t l = reg (GPIOA + GPIO_CFGLR);
    l = (l & ~(0xFu << 4)) | (GPIO_IN_PULL << 4);
    reg (GPIOA + GPIO_CFGLR) = l;
    reg (GPIOA + GPIO_BSHR) = kPin; // pull-up
  }

  bool
  pressed (void)
  {
    return (ch32::reg (ch32::GPIOA + ch32::GPIO_INDR) & kPin) == 0u;
  }
} // namespace key
