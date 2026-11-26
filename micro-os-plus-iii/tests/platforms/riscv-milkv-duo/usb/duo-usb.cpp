/*
 * duo-usb.cpp -- the Milk-V Duo's USB device controller for TinyUSB.
 *
 * duo_usb_hw_init() is BL2's cv_usb_hw_init() (milkv-duo (opensbi)/fsbl/plat/
 * cv180x/usb/cv_usb.c, after U-Boot's board_usb_init() and the Linux
 * clk-cv180x.c gates): the sequence BL2 enumerates as a CVITEK CDC-ACM device
 * with on every boot. The core reset and the device set-up are TinyUSB's
 * (dcd_dwc2.c, slave mode).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <duo-usb.h>

#include <duo-irq.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>

#include "tusb.h"

#include <cstdint>

namespace
{
  constexpr std::uintptr_t TOP_BASE = 0x03000000u;
  constexpr std::uintptr_t CLKGEN_BASE = TOP_BASE + 0x2000u;
  constexpr std::uintptr_t RST_BASE = TOP_BASE + 0x3000u;
  constexpr std::uintptr_t PINMUX_BASE = TOP_BASE + 0x1000u;
  constexpr std::uintptr_t REG_TOP_USB_PHY_CTRL = TOP_BASE + 0x48u;
  constexpr std::uintptr_t REG_USB_ECO_REG = TOP_BASE + 0xB4u; // platform_def.h

  constexpr std::uint32_t USB_PHY_VBUS_VALID = 1u << 0;
  constexpr std::uint32_t USB_PHY_ID_OVERRIDE_ENABLE = 1u << 6;
  constexpr std::uint32_t USB_PHY_ID_VALUE = 1u << 7;
  constexpr std::uint32_t REG_USB_ECO_RXF = 0x80u;

  constexpr unsigned USB_IRQ = 30; // SG2002 TRM table 3.2, C906
  constexpr unsigned USB_PRIO = 1;

  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  void
  udelay (unsigned us)
  {
    const std::uint64_t t0 = riscv::csr::rdtime ();
    const std::uint64_t n
        = static_cast<std::uint64_t> (us) * (riscv_board::TIMEBASE_HZ / 1000000u);
    while (riscv::csr::rdtime () - t0 < n)
      {
      }
  }

  volatile std::uint32_t g_usb_irqs = 0;
  volatile std::uint32_t g_gintsts_seen = 0; // every GINTSTS bit seen at entry

  void
  usb_irq_handler (void)
  {
    g_usb_irqs = g_usb_irqs + 1;
    const std::uint32_t st = reg (0x04340000u + 0x014) & reg (0x04340000u + 0x018);
    g_gintsts_seen = g_gintsts_seen | st;
    tud_int_handler (0);
  }
} // namespace

extern "C"
{
  void
  duo_usb_hw_init (void)
  {
    // Clock gates: clk_axi4_usb, clk_apb_usb, clk_125m_usb, clk_33k_usb =
    // CLK_EN_1[31:28]; clk_12m_usb = CLK_EN_2[0]; both from FPLL, not the
    // XTAL bypass (CLK_BYP_0[17], [18]).
    reg (CLKGEN_BASE + 0x04) |= 0xFu << 28;
    reg (CLKGEN_BASE + 0x08) |= 1u << 0;
    reg (CLKGEN_BASE + 0x30) &= ~((1u << 17) | (1u << 18));

    // USB_VBUS_DET pad -> function 0 (USB_VBUS_DET).
    reg (PINMUX_BASE + 0xAC) = 0;

    // USB soft reset pulse: SOFT_RST0 bit 11, active low.
    reg (RST_BASE + 0x00) &= ~(1u << 11);
    udelay (50);
    reg (RST_BASE + 0x00) |= 1u << 11;
    udelay (50);

    // PHY: VBUS valid (external VBUS) and ID overridden to 1 (B-device).
    reg (REG_TOP_USB_PHY_CTRL) |= USB_PHY_VBUS_VALID
                                  | USB_PHY_ID_OVERRIDE_ENABLE
                                  | USB_PHY_ID_VALUE;
    // ECO: RX FIFO flush enable, as U-Boot does.
    reg (REG_USB_ECO_REG) |= REG_USB_ECO_RXF;

    duo_irq::init ();
    duo_irq::set_handler (USB_IRQ, usb_irq_handler);
    duo_irq::set_priority (USB_IRQ, USB_PRIO);
  }

  std::uint32_t
  duo_usb_irq_count (void)
  {
    return g_usb_irqs;
  }

  std::uint32_t
  duo_usb_gintsts_seen (void)
  {
    return g_gintsts_seen;
  }

  void
  duo_usb_irq_enable (void)
  {
    duo_irq::enable (USB_IRQ);
  }

  void
  duo_usb_irq_disable (void)
  {
    duo_irq::disable (USB_IRQ);
  }

  void
  duo_usb_udelay (unsigned int us)
  {
    udelay (us);
  }
}
