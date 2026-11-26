/*
 * clocks.hpp -- the RP2350 clock tree, core voltage and clock-change reboot
 * (src/clocks.cpp). The Cortex-M pico2 board's bsp/clocks.hpp, same API.
 *
 * clk_sys = clk_peri is chosen at build time or at boot:
 *   default                     150 MHz (PICO2_SYS_CLK_KHZ = 150000);
 *   PICO2_SYS_CLK_KHZ=<kHz>     48000 / 100000 / 120000 / 150000 / 200000 /
 *                               250000 (200 and 250 MHz raise the core
 *                               voltage first: 1.20 V / 1.25 V);
 *   PICO2_CLOCK_FROM_SCRATCH    a row of SYS_TABLE picked by the watchdog
 *                               scratch registers (reboot_to_index()), 48 MHz
 *                               on a cold boot; PICO2_ENABLE_250MHZ adds the
 *                               250 MHz overclock row.
 * mtime / TIMER0 / the watchdog tick from clk_ref (the 12 MHz crystal), so
 * the kernel's time base does not move with clk_sys.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace clocks
{
  // Bring up the clock tree (the board's early start-up calls it).
  void
  init (void);

  // PLL_USB -> clk_usb 48 MHz, for the USB device (before tusb_init()).
  bool
  usb_init (void);

  // One selectable system clock: clk_sys = 12 MHz * fbdiv / pd1 / pd2 (VCO
  // 750..1600 MHz), vsel the POWMAN VREG code (0x0b = 1.10 V). Every row is
  // within 0.03 % of 115200 baud.
  struct sys_entry
  {
    std::uint32_t khz;
    std::uint16_t fbdiv;
    std::uint8_t pd1;
    std::uint8_t pd2;
    std::uint8_t vsel;
  };

#if defined(PICO2_ENABLE_250MHZ)
  inline constexpr unsigned SYS_TABLE_LEN = 13;
#else
  inline constexpr unsigned SYS_TABLE_LEN = 12;
#endif
  extern const sys_entry SYS_TABLE[SYS_TABLE_LEN];

  // What init() selected this boot: the SYS_TABLE row (0xFFFF when the clock
  // came from PICO2_SYS_CLK_KHZ), the clock and the core voltage code.
  unsigned
  current_index (void);
  std::uint32_t
  current_khz (void);
  std::uint8_t
  current_vsel (void);

  // Core voltage (POWMAN VREG): set it BEFORE raising the clock, lower it
  // only after the clock came down. Waits for the regulator.
  void
  vreg_set (std::uint8_t vsel);
  // vsel -> millivolts (0x0b = 1100 mV), for the logs.
  std::uint32_t
  vsel_to_mv (std::uint8_t vsel);

  // clk_adc = PLL_USB 48 MHz, for the ADC (src/adc.cpp).
  void
  adc_clk_init (void);

  // Write the SYS_TABLE row to the watchdog scratch registers and reset
  // through the watchdog; init() then boots at that clock. Never returns.
  [[noreturn]] void
  reboot_to_index (unsigned idx);
} // namespace clocks
