/*
 * ch32-usb.h -- bring-up of the CH32V307's high-speed USB (USBHS, the EVT's
 * port P6) for TinyUSB.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef CH32V307_EVT_CH32_USB_H_
#define CH32V307_EVT_CH32_USB_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // Clock and PHY for USBHS; call once, before tusb_init(). False when the
  // system clock is not the 144 MHz from HSE the set-up assumes.
  bool ch32_usb_hw_init (void);

#ifdef __cplusplus
}
#endif

#endif /* CH32V307_EVT_CH32_USB_H_ */
