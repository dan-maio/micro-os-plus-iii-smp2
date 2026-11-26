/*
 * ch32-usb.cpp -- the CH32V307's high-speed USB (USBHS, port P6) for
 * TinyUSB's WCH driver (portable/wch/dcd_ch32_usbhs.c).
 *
 * The driver sets the controller up (dcd_init) but not its clocks: TinyUSB's
 * own CH32V30x board code does that with six WCH library calls
 * (hw/bsp/ch32v30x/family.c, board_init(), at the TinyUSB commit this tree
 * is). They are register writes (WCH EVT ch32v30x_rcc.c), done here directly:
 *
 *   RCC_USBCLK48MConfig (USBPHY)        CFGR2 bit 31     = 1  48 MHz from the USB PHY
 *   RCC_USBHSPHYPLLALIVEcmd (ENABLE)    CFGR2 bit 30     = 1  PHY PLL kept alive
 *   RCC_USBHSPLLCKREFCLKConfig (4M)     CFGR2 bits 29:28 = 01 4 MHz reference
 *   RCC_USBHSPLLCLKConfig (HSE)         CFGR2 bit 27     = 0  PLL from HSE
 *   RCC_USBHSConfig (Div2)              CFGR2 bits 26:24 = 001 HSE / 2 = 4 MHz
 *   RCC_AHBPeriphClockCmd (USBHS)       AHBPCENR bit 11  = 1
 *
 * The interrupt: PFIC 85, WCH's USBHS_IRQHandler (the board's src/irq.cpp
 * dispatches by WCH's names) -> tud_int_handler(0), which queues TinyUSB's
 * events; a thread runs tud_task(). It does not call the kernel, and runs
 * at the kernel-aware priority.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <board.hpp>
#include <pfic.hpp>

#include "ch32-usb.h"
#include "tusb.h"

namespace
{
  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  constexpr std::uintptr_t RCC_AHBPCENR = 0x40021014u;
  constexpr std::uintptr_t RCC_CFGR2 = 0x4002102Cu;
  constexpr std::uint32_t AHB_USBHS = 1u << 11;

  constexpr unsigned PFIC_USBHS = 85;
} // namespace

extern "C" bool
ch32_usb_hw_init (void)
{
  if (!board::clock_from_hse ())
    {
      return false;
    }
  std::uint32_t c = reg (RCC_CFGR2);
  c &= ~((1u << 31) | (1u << 30) | (3u << 28) | (1u << 27) | (7u << 24));
  c |= (1u << 31) | (1u << 30) | (1u << 28) | (0u << 27) | (1u << 24);
  reg (RCC_CFGR2) = c;
  reg (RCC_AHBPCENR) |= AHB_USBHS;

  pfic::set_priority (PFIC_USBHS, pfic::PRIO_KERNEL_HIGH);
  return true;
}

extern "C" void
USBHS_IRQHandler (void)
{
  tud_int_handler (0);
}
