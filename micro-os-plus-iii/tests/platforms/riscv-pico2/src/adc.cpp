/*
 * adc.cpp -- RP2350 12-bit SAR ADC, bare registers: the Cortex-M pico2
 * board's glue/adc.cpp. Datasheet sequence: out of reset, CS.EN and wait
 * for CS.READY; per conversion AINSEL + START_ONCE, poll READY.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <adc.hpp>
#include <rp2350.hpp>

using rp2350::reg;

namespace
{
  constexpr std::uint32_t ADC_CS = 0x00, ADC_RESULT = 0x04;
  constexpr std::uint32_t CS_EN = 1u << 0, CS_START_ONCE = 1u << 2,
                          CS_READY = 1u << 8;
  constexpr std::uint32_t CS_AINSEL_LSB = 12, CS_AINSEL_BITS = 0xF000u;
  constexpr std::uint32_t RESET_ADC = 1u << 0;
  constexpr std::uint32_t PAD_IE = 1u << 6, PAD_OD = 1u << 7;
} // namespace

namespace adc
{
  void
  init (void)
  {
    rp2350::unreset_block (RESET_ADC);
    reg (rp2350::ADC_BASE + ADC_CS) = CS_EN;
    while (!(reg (rp2350::ADC_BASE + ADC_CS) & CS_READY))
      {
      }
  }

  void
  gpio_init (unsigned gpio)
  {
    // The digital side off so the analog input is clean: IE = 0, OD = 1.
    const std::uint32_t pad = rp2350::PADS_BANK0_BASE + 0x04u + gpio * 4u;
    reg (pad) = (reg (pad) & ~PAD_IE) | PAD_OD;
  }

  std::uint16_t
  read (unsigned channel)
  {
    const std::uint32_t b = rp2350::ADC_BASE;
    reg (b + ADC_CS) = (reg (b + ADC_CS) & ~CS_AINSEL_BITS) | CS_EN
                       | ((channel << CS_AINSEL_LSB) & CS_AINSEL_BITS)
                       | CS_START_ONCE;
    while (!(reg (b + ADC_CS) & CS_READY))
      {
      }
    return static_cast<std::uint16_t> (reg (b + ADC_RESULT) & 0x0FFFu);
  }
} // namespace adc
