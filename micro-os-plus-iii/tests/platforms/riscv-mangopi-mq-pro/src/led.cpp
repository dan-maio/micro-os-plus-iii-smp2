/*
 * led.cpp -- the MangoPi MQ-Pro onboard blue status LED: PD18, active high.
 *
 *   PD_CFG2  0x02000098  bits 11:8  = 1 (GPIO output)
 *   PD_DAT   0x020000a0  bit  18
 *   PD_PULL2 0x020000bc  bits  5:4  = 0 (no pull)
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <led.hpp>

namespace
{
  constexpr std::uintptr_t GPIO_BASE = 0x02000000u;
  constexpr std::uintptr_t PD_CFG2 = GPIO_BASE + 0x98u;
  constexpr std::uintptr_t PD_DAT = GPIO_BASE + 0xA0u;
  constexpr std::uintptr_t PD_PULL2 = GPIO_BASE + 0xBCu;

  constexpr std::uint32_t LED_BIT = 1u << 18;
  constexpr std::uint32_t CFG_MASK = 0xFu << 8;
  constexpr std::uint32_t CFG_VAL = 1u << 8;   // GPIO output
  constexpr std::uint32_t PULL_MASK = 0x3u << 4;

  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }
} // namespace

namespace led
{
  void
  init (void)
  {
    reg (PD_CFG2) = (reg (PD_CFG2) & ~CFG_MASK) | CFG_VAL;
    reg (PD_PULL2) = reg (PD_PULL2) & ~PULL_MASK;
    off ();
  }

  void
  on (void)
  {
    reg (PD_DAT) = reg (PD_DAT) | LED_BIT;
  }

  void
  off (void)
  {
    reg (PD_DAT) = reg (PD_DAT) & ~LED_BIT;
  }

  void
  toggle (void)
  {
    reg (PD_DAT) = reg (PD_DAT) ^ LED_BIT;
  }

  bool
  is_on (void)
  {
    return (reg (PD_DAT) & LED_BIT) != 0;
  }
} // namespace led
