/*
 * led.cpp -- the VisionFive 2 Lite's user LED: AON GPIO 3, active high.
 *
 * AON IOMUX (aongpio, 0x17020000), JH7110 TRM 2.8.8 "AON IOMUX CFG":
 *   0x0  SYSCFG FMUX 0  doen, one byte per GPIO0..3, bits [2:0]: the OEN
 *                       index (Table 2-42): 0 = 1'd0 = output enabled
 *                       (OEN is active low), 1 = 1'd1 = disabled
 *   0x4  SYSCFG FMUX 1  dout, one byte per GPIO0..3, bits [3:0]: the output
 *                       index (Table 2-42): 0 = 1'd0 (low), 1 = 1'd1 (high)
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <led.hpp>

namespace
{
  constexpr std::uintptr_t AON_IOMUX = 0x17020000u;
  constexpr std::uintptr_t DOEN = AON_IOMUX + 0x0u;
  constexpr std::uintptr_t DOUT = AON_IOMUX + 0x4u;

  constexpr unsigned PIN = 3;
  constexpr unsigned SHIFT = 8u * PIN;
  constexpr std::uint32_t DOEN_MASK = 0x7u << SHIFT;
  constexpr std::uint32_t DOUT_MASK = 0xFu << SHIFT;

  inline volatile std::uint32_t&
  reg (std::uintptr_t addr)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (addr);
  }

  void
  set (bool high)
  {
    const std::uint32_t v = reg (DOUT) & ~DOUT_MASK;
    reg (DOUT) = v | ((high ? 1u : 0u) << SHIFT);
  }
} // namespace

namespace led
{
  void
  init (void)
  {
    off ();
    reg (DOEN) = reg (DOEN) & ~DOEN_MASK; // index 0: output enabled
  }

  void
  on (void)
  {
    set (true);
  }

  void
  off (void)
  {
    set (false);
  }

  void
  toggle (void)
  {
    set (!is_on ());
  }

  bool
  is_on (void)
  {
    return ((reg (DOUT) & DOUT_MASK) >> SHIFT) == 1u;
  }
} // namespace led
