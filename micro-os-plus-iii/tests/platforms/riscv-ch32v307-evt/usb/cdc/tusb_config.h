/*
 * tusb_config.h -- TinyUSB for the CH32V307-EVT's high-speed port P6: the
 * USBHS controller (TinyUSB's portable/wch/dcd_ch32_usbhs.c), device only,
 * one CDC-ACM interface, no RTOS integration (CFG_TUSB_OS none): the USBHS
 * interrupt queues TinyUSB's events (usb/ch32-usb.cpp) and a µOS++ thread
 * runs tud_task().
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU OPT_MCU_CH32V307
// The V307 has both controllers; TinyUSB's default is the HS one. Say so.
#define CFG_TUD_WCH_USBIP_USBHS 1
#define CFG_TUD_WCH_USBIP_USBFS 0

#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#define CFG_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__ ((aligned (4)))
#endif

#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID 0
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

// High speed: 512-byte bulk packets. The FIFOs hold a few packets each way.
#define CFG_TUD_CDC_RX_BUFSIZE 2048
#define CFG_TUD_CDC_TX_BUFSIZE 2048
#define CFG_TUD_CDC_EP_BUFSIZE 512

#endif /* _TUSB_CONFIG_H_ */
