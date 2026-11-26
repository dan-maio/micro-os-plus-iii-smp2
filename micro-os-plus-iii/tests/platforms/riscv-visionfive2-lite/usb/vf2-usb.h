/*
 * vf2-usb.h -- the VisionFive 2 Lite's USB device port: the JH-7110's
 * Cadence USBSS-DRD (cdns3 v1) on the USB-C connector.
 *
 * Board (VF2_LITE_V1.10 schematic, page 22): the USB-C J13's data pins go
 * to HSD2 of U27 (FSUSB42 2:1 switch); its common side is the JH-7110's
 * USB 2.0 PHY; SEL = USB_SEL_GPIO62: 0 = HSD1 = the USB-A hub ("Host"),
 * 1 = HSD2 = the USB-C ("Device"). GPIO26 (EN_USB_PWR) powers the USB-A
 * ports only and is left alone.
 *
 * vf2_usb_hw_init() is the bring-up measured on the board (2026-11-07: the
 * PC logged "new high-speed USB device"), each step from Linux:
 *   cdns3-starfive.c  stg_syscon+4: MISC_CFG = SUSPENDM_BYPS|PLL_EN|
 *                     REFCLK_MODE, STRAP = DEVICE, SUSPENDM = 0
 *   stgcrg            USB0 clocks 1..6 on, resets 7..10 released
 *   phy-jh7110-usb.c  CLK_MODE RX_NORMAL_PWR, LS keep-alive off,
 *                     sys_syscon+0x18 USB_PDRSTN_SPLIT
 *   drd.c             OTG v1 (DID 0x000402xx): simulate = 1,
 *                     OTGCMD = DEV_BUS_REQ | OTG_DIS, wait DEV_READY
 *   GPIO62 = 1        the switch to the USB-C
 *
 * DMA is not coherent (Linux: the L2 controller's FLUSH64 for every DMA
 * sync on JH-7110, sifive_ccache.c): vf2_dcache_flush() writes back and
 * invalidates a range, line by line.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef VF2_USB_H_
#define VF2_USB_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Returns false (and leaves the controller off) if the OTG block does not
  // report a cdns3 v1 DID or never reaches DEV_READY.
  bool vf2_usb_hw_init (void);

  // Write back + invalidate [addr, addr + len) in the L2 (and above).
  void vf2_dcache_flush (const void* addr, uint32_t len);

  void vf2_usb_udelay (uint32_t us);

  // The last OTG DID / DEV_READY status seen by vf2_usb_hw_init().
  uint32_t vf2_usb_otg_did (void);

#ifdef __cplusplus
}
#endif

#endif // VF2_USB_H_
