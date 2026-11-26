/* tusb_config.h -- TinyUSB config for the VisionFive 2 Lite USB-CDC-ACM
 * device (JH-7110 Cadence USBSS-DRD, usb/cdns3/dcd_cdns3.c). */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#define CFG_TUSB_MCU OPT_MCU_JH7110 /* StarFive JH-7110, Cadence cdns3 */

/* pico-sdk placement macros used by tusb_mcu.h before pico.h is seen. */
#ifndef __not_in_flash
#define __not_in_flash(group)
#endif
#ifndef __not_in_flash_func
#define __not_in_flash_func(f) f
#endif

#define CFG_TUSB_OS OPT_OS_NONE /* pumped by a µOS++ thread */
/* Full speed: the driver forces it (USB_CONF_SFORCE_FS), so the 64-byte
 * descriptors of the portable CDC glue are valid. */
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
/* The controller DMAs to and from these buffers and the JH-7110's DMA is not
 * cache-coherent: one 64-byte cache line per buffer start, so a flush never
 * touches another object's data. */
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__ ((aligned (64)))
#endif

#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_HID 0
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

#define CFG_TUD_CDC_RX_BUFSIZE 256
#define CFG_TUD_CDC_TX_BUFSIZE 512
#define CFG_TUD_CDC_EP_BUFSIZE 64

#endif /* _TUSB_CONFIG_H_ */
