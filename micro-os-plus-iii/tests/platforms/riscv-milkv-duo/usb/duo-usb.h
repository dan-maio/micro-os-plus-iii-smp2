/*
 * duo-usb.h -- the Milk-V Duo's USB device controller (DWC2 at 0x04340000):
 * hardware bring-up and the interrupt glue for TinyUSB (duo-usb.cpp).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef DUO_USB_H_
#define DUO_USB_H_

#ifdef __cplusplus
extern "C"
{
#endif

  // Clocks, VBUS-detect pad, USB soft reset pulse, PHY as a B-device (VBUS
  // valid + ID override), ECO RX-FIFO flush: BL2's cv_usb_hw_init(), the
  // U-Boot board_usb_init() values. Then the PLIC line (30) gets its handler
  // (tud_int_handler) and a priority; tusb_init() enables it.
  void duo_usb_hw_init (void);

  void duo_usb_irq_enable (void);
  void duo_usb_irq_disable (void);
  void duo_usb_udelay (unsigned int us);

  // USB interrupts taken so far (diagnostics).
  unsigned int duo_usb_irq_count (void);
  // The OR of every (masked) GINTSTS seen at interrupt entry (diagnostics).
  unsigned int duo_usb_gintsts_seen (void);

#ifdef __cplusplus
}
#endif
#endif
