/*
 * smp-test-usb-cdc-acm (VisionFive 2 Lite) -- a USB CDC-ACM device on the
 * USB-C connector, with TinyUSB and the board's Cadence USBSS-DRD driver
 * (test/boards/visionfive2-lite/usb/cdns3/dcd_cdns3.c), on all four U74.
 *
 * The test of the same name on the Milk-V Duo and the Pico 2, with this
 * board's bring-up (vf2_usb_hw_init(): clocks, resets, PHY, OTG device
 * mode, GPIO62 -> the USB-C) and a polled controller (the USB thread calls
 * tud_int_handler()). The USB and console threads are pinned to CPU 0: the
 * CDC glue's TX ring takes producers on one core only (usb_cdc.c).
 *
 * The device enumerates as Cafe:4013 (full speed), echoes each line the
 * host sends ("you typed: ..."), and ends with PASS when a line contains
 * Escape. The host side: test/boards/pico2/usb-host.py cdc. FAIL when the
 * bring-up fails, or when the host has not configured the device within
 * 60 s (the controller status is printed).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <led.hpp>
#include <riscv-smp.hpp>
#include <uart.hpp>
#include <vf2-usb.h>

#include <hw_result.hpp>

#include "tusb.h"
#include "usb_cdc.h"

#include <cstdint>
#include <cstdio>

using namespace os::rtos;

namespace
{
  volatile bool usb_up = false;

  std::uint32_t
  dev_reg (std::uint32_t off)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (0x10120000u + off);
  }

  void
  fail (const char* why)
  {
    char b[160];
    std::snprintf (b, sizeof (b),
                   "[usb] usb_conf=0x%08x usb_sts=0x%08x ep_ists=0x%08x\n",
                   static_cast<unsigned> (dev_reg (0x00)),
                   static_cast<unsigned> (dev_reg (0x04)),
                   static_cast<unsigned> (dev_reg (0x40)));
    uart::write (b);
    std::snprintf (b, sizeof (b), "\nRESULT: FAIL (%s)\n", why);
    uart::write (b);
    hw_result::fail ();
  }

  void*
  usb_device_task (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    while (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }
    if (!vf2_usb_hw_init ())
      {
        char b[64];
        std::snprintf (b, sizeof (b), "[usb] bring-up failed, OTG DID 0x%08x\n",
                       static_cast<unsigned> (vf2_usb_otg_did ()));
        uart::write (b);
        fail ("USB bring-up");
      }
    tusb_init ();
    usb_up = true;
    uart::write ("[usb] tusb_init done (Cadence cdns3, full speed); waiting "
                 "for the host\n");
    for (;;)
      {
        tud_int_handler (0); // polled controller
        if (tud_task_event_ready ())
          {
            tud_task ();
          }
        mu_usb_cdc_task ();
        this_thread::yield ();
      }
    return nullptr;
  }

  void*
  console_task (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    while (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }
    static char line[MU_CDC_RX_BUFSIZE];
    bool was_mounted = false;
    const auto t0 = sysclock.now ();
    for (;;)
      {
        const bool m = mu_usb_cdc_mounted () != 0;
        if (m != was_mounted)
          {
            uart::write (m ? "[usb] mounted (configured by the host)\n"
                           : "[usb] unmounted\n");
            was_mounted = m;
          }
        if (!m && usb_up && sysclock.now () - t0 > 60000)
          {
            fail ("not configured by the host within 60 s");
          }
        int n = mu_usb_cdc_readline (line, sizeof (line));
        if (n >= 0)
          {
            for (int i = 0; i < n; ++i)
              {
                if (static_cast<unsigned char> (line[i]) == 0x1B)
                  {
                    uart::write ("\nRESULT: PASS (host sent Escape)\n");
                    hw_result::ok ();
                  }
              }
            uart::write ("[line] ");
            uart::write (line, static_cast<std::size_t> (n));
            uart::write ("\n");
            mu_usb_cdc_write_str ("you typed: ");
            mu_usb_cdc_write (line, static_cast<std::uint32_t> (n));
            mu_usb_cdc_write_str ("\r\n");
            mu_usb_cdc_flush ();
          }
        sysclock.sleep_for (10);
      }
    return nullptr;
  }

  thread::stack::element_t usb_stack[4096];
  thread::stack::element_t console_stack[4096];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::write ("\n=============================================\n");
  uart::write ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  uart::write ("  smp-test-usb-cdc-acm : USB CDC-ACM (Cadence cdns3)\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = usb_stack;
  attr.th_stack_size_bytes = sizeof (usb_stack);
  static thread usb_th{ "usb", usb_device_task, nullptr, attr };
  attr.th_stack_address = console_stack;
  attr.th_stack_size_bytes = sizeof (console_stack);
  static thread con_th{ "console", console_task, nullptr, attr };

  for (;;)
    {
      led::toggle ();
      sysclock.sleep_for (500);
    }
  return 0;
}
