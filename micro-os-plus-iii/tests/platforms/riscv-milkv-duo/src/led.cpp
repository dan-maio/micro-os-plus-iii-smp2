/*
 * led.cpp -- the Milk-V Duo's blue LED: GPIOC (gpio2) bit 24.
 *
 *   gpio2: gpio@03022000 (snps,dw-apb-gpio)    cv180x_base.dtsi
 *   clk_apb_gpio = CLK_EN_0 (0x03002000) bit 29  clk-cv180x.c
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <led.hpp>

namespace
{
  constexpr std::uintptr_t CLK_EN_0 = 0x03002000u;
  constexpr std::uint32_t CLK_APB_GPIO = 1u << 29;

  constexpr std::uintptr_t GPIOC_BASE = 0x03022000u;
  constexpr unsigned SWPORTA_DR = 0x00;
  constexpr unsigned SWPORTA_DDR = 0x04;

  constexpr std::uint32_t LED_BIT = 1u << 24;

  inline volatile std::uint32_t&
  reg (std::uintptr_t addr)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (addr);
  }
} // namespace

namespace led
{
  void
  init (void)
  {
    reg (CLK_EN_0) = reg (CLK_EN_0) | CLK_APB_GPIO;
    reg (GPIOC_BASE + SWPORTA_DDR) = reg (GPIOC_BASE + SWPORTA_DDR) | LED_BIT;
    off ();
  }

  void
  on (void)
  {
    reg (GPIOC_BASE + SWPORTA_DR) = reg (GPIOC_BASE + SWPORTA_DR) | LED_BIT;
  }

  void
  off (void)
  {
    reg (GPIOC_BASE + SWPORTA_DR) = reg (GPIOC_BASE + SWPORTA_DR) & ~LED_BIT;
  }

  void
  toggle (void)
  {
    reg (GPIOC_BASE + SWPORTA_DR) = reg (GPIOC_BASE + SWPORTA_DR) ^ LED_BIT;
  }

  bool
  is_on (void)
  {
    return (reg (GPIOC_BASE + SWPORTA_DR) & LED_BIT) != 0;
  }
} // namespace led
