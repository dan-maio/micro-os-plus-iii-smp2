/*
 * smp-test-usb-hid on the Milk-V Duo: the Cortex-M Pico 2 test
 * (micro-os-plus-iii-cortexm, test/pico2/smp-test-usb-hid) through its
 * RISC-V Pico 2 port, on the CV1800B's DWC2 controller (TinyUSB DWC2, slave
 * mode; see smp-test-usb-cdc-acm). One hart.
 *
 * The device enumerates as Cafe:4012 (generic HID); the host sends OUTPUT
 * reports {length, chars...} (test/boards/pico2/usb-host.py hid, or the
 * Pico 2 test's host/kbd_forward); the received characters are printed on
 * the UART, and Escape ends the run with PASS.
 */

#include <cmsis-plus/rtos/os.h>

#include <duo-usb.h>
#include <led.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include "tusb.h"

#include <cstdint>

extern "C"
{
  int mu_usb_hid_getchar (void);
  int mu_usb_hid_mounted (void);
}

using namespace os::rtos;

namespace
{
  void*
  usb_device_task (void*)
  {
    duo_usb_hw_init ();
    tusb_init ();
    uart::write ("[usb] tusb_init done; waiting for the host\n");
    for (;;)
      {
        if (tud_task_event_ready ())
          {
            tud_task ();
          }
        this_thread::yield ();
      }
    return nullptr;
  }

  void*
  consumer_task (void*)
  {
    bool was_mounted = false;
    for (;;)
      {
        const bool m = mu_usb_hid_mounted () != 0;
        if (m != was_mounted)
          {
            uart::write (m ? "[usb] mounted\n" : "[usb] unmounted\n");
            was_mounted = m;
          }
        int c = mu_usb_hid_getchar ();
        if (c >= 0)
          {
            if (c == 0x1B)
              {
                uart::write ("\nRESULT: PASS (host sent Escape)\n");
                hw_result::ok ();
              }
            uart::put_char (static_cast<char> (c));
            continue;
          }
        sysclock.sleep_for (10);
      }
    return nullptr;
  }

  thread::stack::element_t usb_stack[4096];
  thread::stack::element_t consumer_stack[4096];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::write ("\n=============================================\n");
  uart::write ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  uart::write ("  smp-test-usb-hid : USB HID (DWC2)\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = usb_stack;
  attr.th_stack_size_bytes = sizeof (usb_stack);
  static thread usb_th{ "usb", usb_device_task, nullptr, attr };
  attr.th_stack_address = consumer_stack;
  attr.th_stack_size_bytes = sizeof (consumer_stack);
  static thread con_th{ "consumer", consumer_task, nullptr, attr };

  for (;;)
    {
      led::toggle ();
      sysclock.sleep_for (500);
    }
  return 0;
}
