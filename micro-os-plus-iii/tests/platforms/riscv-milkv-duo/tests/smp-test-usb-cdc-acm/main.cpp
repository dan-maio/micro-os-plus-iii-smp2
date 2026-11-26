/*
 * smp-test-usb-cdc-acm on the Milk-V Duo: the Cortex-M Pico 2 test
 * (micro-os-plus-iii-cortexm, test/pico2/smp-test-usb-cdc-acm) through its
 * RISC-V Pico 2 port, on the CV1800B's DWC2 controller: TinyUSB's DWC2
 * driver in slave mode (test/boards/milkv-duo/usb), the controller set up as
 * BL2 does it, its interrupt (PLIC 30) through duo_irq. One hart; no WS2812.
 *
 * The device enumerates as Cafe:4013 (CDC-ACM), echoes each line the host
 * sends ("you typed: ..."), and ends with PASS when a line contains Escape.
 * The host side: test/boards/pico2/usb-host.py cdc (or a terminal). The Duo's
 * USB-C must be connected to the PC.
 */

#include <cmsis-plus/rtos/os.h>

#include <duo-usb.h>
#include <led.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include "tusb.h"
#include "usb_cdc.h"

#include <cstdint>

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
        mu_usb_cdc_task ();
        this_thread::yield ();
      }
    return nullptr;
  }

  void*
  console_task (void*)
  {
    static char line[MU_CDC_RX_BUFSIZE];
    bool was_mounted = false;
    for (;;)
      {
        const bool m = mu_usb_cdc_mounted () != 0;
        if (m != was_mounted)
          {
            uart::write (m ? "[usb] mounted\n" : "[usb] unmounted\n");
            was_mounted = m;
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
  uart::write ("  smp-test-usb-cdc-acm : USB CDC-ACM (DWC2)\n");
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
