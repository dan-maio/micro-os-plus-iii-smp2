/*
 * vf2-usb.cpp -- USB device bring-up and DMA cache maintenance for the
 * VisionFive 2 Lite (vf2-usb.h).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <vf2-usb.h>

#include <clint.hpp>
#include <riscv-board.hpp>

#include <cstdint>

namespace
{
  // jh7110.dtsi / measured
  constexpr std::uintptr_t STG_SYSCON_USB = 0x10240004u; // starfive,stg-syscon <.. 0x4>
  constexpr std::uintptr_t STGCRG = 0x10230000u;
  constexpr std::uintptr_t STGCRG_RST_ASSERT = STGCRG + 0x74u;
  constexpr std::uintptr_t STGCRG_RST_STATUS = STGCRG + 0x78u;
  constexpr std::uintptr_t USBPHY0 = 0x10200000u;
  constexpr std::uintptr_t SYS_SYSCON_USB_SPLIT = 0x13030018u;
  constexpr std::uintptr_t OTG = 0x10100000u;
  constexpr std::uintptr_t SYSGPIO = 0x13040000u;
  constexpr std::uintptr_t CCACHE_FLUSH64 = 0x02010000u + 0x200u;

  constexpr unsigned GPIO_USB_SEL = 62;

  std::uint32_t otg_did = 0;

  volatile std::uint32_t&
  r32 (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  void
  rmw (std::uintptr_t a, std::uint32_t mask, std::uint32_t val)
  {
    r32 (a) = (r32 (a) & ~mask) | (val & mask);
  }

  std::uint64_t
  us_ticks (std::uint32_t us)
  {
    return static_cast<std::uint64_t> (us) * (riscv_board::TIMEBASE_HZ / 1000000u);
  }
} // namespace

extern "C" void
vf2_usb_udelay (std::uint32_t us)
{
  const std::uint64_t end = clint::now () + us_ticks (us);
  while (clint::now () < end)
    {
    }
}

extern "C" std::uint32_t
vf2_usb_otg_did (void)
{
  return otg_did;
}

extern "C" void
vf2_dcache_flush (const void* addr, std::uint32_t len)
{
  if (len == 0)
    {
      return;
    }
  const std::uintptr_t start = reinterpret_cast<std::uintptr_t> (addr);
  const std::uintptr_t end = start + len;
  __asm__ volatile("fence rw, rw" ::: "memory");
  for (std::uintptr_t line = start & ~static_cast<std::uintptr_t> (63);
       line < end; line += 64)
    {
      *reinterpret_cast<volatile std::uint64_t*> (CCACHE_FLUSH64) = line;
    }
  __asm__ volatile("fence rw, rw" ::: "memory");
}

extern "C" bool
vf2_usb_hw_init (void)
{
  // 0. A previous image may have left the device connected and configured
  //    (measured: no disconnect on the PC between two runs, usb_sts still
  //    ADDRESSED | CFGSTS, so the host never re-enumerated). If USB0 is out
  //    of reset: drop the D+ pull-up (USB_CONF.DEVDS) long enough for the
  //    host to see the disconnect, then put the whole block back in reset.
  if ((r32 (STGCRG_RST_STATUS) & 0x780u) == 0x780u)
    {
      r32 (0x10120000u) = 1u << 15; // device USB_CONF: DEVDS
      vf2_usb_udelay (200000);
      rmw (STGCRG_RST_ASSERT, 0x780u, 0x780u);
      const std::uint64_t end = clint::now () + us_ticks (10000);
      while ((r32 (STGCRG_RST_STATUS) & 0x780u) != 0)
        {
          if (clint::now () > end)
            {
              break;
            }
        }
      vf2_usb_udelay (1000);
    }

  // 1. stg syscon: MISC_CFG[23:20] = SUSPENDM_BYPS | PLL_EN | REFCLK_MODE,
  //    STRAP[18:16] = DEVICE (bit 18), SUSPENDM[19] = 0 (cdns3-starfive.c).
  rmw (STG_SYSCON_USB, 0x00F00000u, (1u << 20) | (1u << 22) | (1u << 23));
  rmw (STG_SYSCON_USB, 0x00070000u, 1u << 18);
  rmw (STG_SYSCON_USB, 0x00080000u, 0);

  // 2. USB0 clocks (stgcrg 1..6: apb, utmi_apb, axi, lpm, stb, app_125) on,
  //    then the resets (axi 7, apb 8, utmi_apb 9, pwrup 10) released.
  for (unsigned idx = 1; idx <= 6; ++idx)
    {
      rmw (STGCRG + 4u * idx, 0x80000000u, 0x80000000u);
    }
  rmw (STGCRG_RST_ASSERT, 0x780u, 0);
  {
    const std::uint64_t end = clint::now () + us_ticks (10000);
    while ((r32 (STGCRG_RST_STATUS) & 0x780u) != 0x780u)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
  }

  // 3. PHY (phy-jh7110-usb.c): RX normal power; device mode: LS keep-alive
  //    off; connect the USB 2.0 PHY (USB_PDRSTN_SPLIT).
  rmw (USBPHY0 + 0x0u, 1u << 1, 1u << 1);
  rmw (USBPHY0 + 0x4u, 1u << 4, 0);
  rmw (SYS_SYSCON_USB_SPLIT, 1u << 17, 1u << 17);

  // 4. OTG / DRD (drd.c): a cdns3 v1 block, put in device mode.
  otg_did = r32 (OTG + 0x00u);
  if ((otg_did & 0xFFFFFF00u) != 0x00040200u)
    {
      return false;
    }
  r32 (OTG + 0x40u) = 1u;                    // simulate (drd.c, CDNS3 v1)
  r32 (OTG + 0x10u) = (1u << 0) | (1u << 3); // DEV_BUS_REQ | OTG_DIS
  {
    const std::uint64_t end = clint::now () + us_ticks (100000);
    while ((r32 (OTG + 0x14u) & (1u << 27)) == 0) // OTGSTS_CDNS3_DEV_READY
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
  }

  // 5. GPIO62 = 1: the switch U27 to the USB-C (sysgpio DOUT/DOEN byte
  //    lanes, pinctrl-starfive-jh7110.c; DOEN 0 = output enabled).
  {
    const std::uintptr_t off = 4u * (GPIO_USB_SEL / 4u);
    const unsigned shift = 8u * (GPIO_USB_SEL % 4u);
    rmw (SYSGPIO + 0x040u + off, 0x7Fu << shift, 1u << shift);
    rmw (SYSGPIO + 0x000u + off, 0x3Fu << shift, 0);
  }
  vf2_usb_udelay (1000);
  return true;
}
