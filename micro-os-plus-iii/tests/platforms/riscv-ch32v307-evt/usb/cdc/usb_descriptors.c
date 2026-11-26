/*
 * usb_descriptors.c -- one CDC-ACM interface for the CH32V307-EVT's
 * high-speed port P6.
 *
 * The Pico 2's descriptors (test/boards/pico2/usb/cdc/usb_descriptors.c,
 * TinyUSB's cdc example), made high-speed capable the way TinyUSB's own
 * cdc_msc example is: a high-speed device must answer the device
 * qualifier and the other-speed configuration, and its bulk endpoints are
 * 512 bytes at high speed (64 at full speed). The configuration returned is
 * the one for the speed the link came up at.
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org), MIT. Adapted 2026 Dan.
 */

#include <string.h>

#include "tusb.h"

#define USB_PID 0x4014

static tusb_desc_device_t const desc_device = {
  .bLength = sizeof (tusb_desc_device_t),
  .bDescriptorType = TUSB_DESC_DEVICE,
  .bcdUSB = 0x0200,
  // IAD for CDC, so Windows binds the ACM driver too.
  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor = 0xCafe,
  .idProduct = USB_PID,
  .bcdDevice = 0x0100,
  .iManufacturer = 0x01,
  .iProduct = 0x02,
  .iSerialNumber = 0x03,
  .bNumConfigurations = 0x01,
};

uint8_t const*
tud_descriptor_device_cb (void)
{
  return (uint8_t const*) &desc_device;
}

enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82

static uint8_t const desc_fs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR (1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR (ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT,
                      EPNUM_CDC_IN, 64),
};

#if TUD_OPT_HIGH_SPEED
static uint8_t const desc_hs_configuration[] = {
  TUD_CONFIG_DESCRIPTOR (1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
  TUD_CDC_DESCRIPTOR (ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 16, EPNUM_CDC_OUT,
                      EPNUM_CDC_IN, 512),
};

static uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN];

static tusb_desc_device_qualifier_t const desc_device_qualifier = {
  .bLength = sizeof (tusb_desc_device_qualifier_t),
  .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
  .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_MISC,
  .bDeviceSubClass = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
  .bNumConfigurations = 0x01,
  .bReserved = 0x00,
};

uint8_t const*
tud_descriptor_device_qualifier_cb (void)
{
  return (uint8_t const*) &desc_device_qualifier;
}

// The configuration for the OTHER speed, typed OTHER_SPEED_CONFIG.
uint8_t const*
tud_descriptor_other_speed_configuration_cb (uint8_t index)
{
  (void) index;
  memcpy (desc_other_speed_config,
          (tud_speed_get () == TUSB_SPEED_HIGH) ? desc_fs_configuration
                                                : desc_hs_configuration,
          CONFIG_TOTAL_LEN);
  desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
  return desc_other_speed_config;
}
#endif

uint8_t const*
tud_descriptor_configuration_cb (uint8_t index)
{
  (void) index;
#if TUD_OPT_HIGH_SPEED
  // High-speed capable; the host (or a hub) may still be full speed.
  return (tud_speed_get () == TUSB_SPEED_HIGH) ? desc_hs_configuration
                                               : desc_fs_configuration;
#else
  return desc_fs_configuration;
#endif
}

enum
{
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC,
};

static char const* string_desc_arr[] = {
  (const char[]) { 0x09, 0x04 }, // English (0x0409)
  "TinyUSB",                     // manufacturer
  "CH32V307 CDC-ACM",            // product
  "CH32V307-0001",               // serial (fixed; no unique-ID read)
  "CH32V307 CDC-ACM data",       // CDC interface
};

static uint16_t _desc_str[32 + 1];

uint16_t const*
tud_descriptor_string_cb (uint8_t index, uint16_t langid)
{
  (void) langid;
  size_t chr_count;
  if (index == STRID_LANGID)
    {
      memcpy (&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
    }
  else
    {
      if (!(index < sizeof (string_desc_arr) / sizeof (string_desc_arr[0])))
        {
          return NULL;
        }
      const char* str = string_desc_arr[index];
      chr_count = strlen (str);
      const size_t max_count = sizeof (_desc_str) / sizeof (_desc_str[0]) - 1;
      if (chr_count > max_count)
        {
          chr_count = max_count;
        }
      for (size_t i = 0; i < chr_count; i++)
        {
          _desc_str[1 + i] = (uint16_t) str[i];
        }
    }
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
