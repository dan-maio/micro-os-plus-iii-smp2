/*
 * wifi-ram-disk (MangoPi MQ-Pro) -- files over Wi-Fi into a 512 MiB RAM disk,
 * under the µOS++ scheduler.
 *
 * The MangoPi counterpart of the VisionFive 2 Lite's vf2-wifi-ram-disk: the
 * same RAM disk, the same file server and the same PC tools, on this board's
 * radio (an RTL8723DS on SDC1, the wifi-net stack, unchanged).  The board has
 * no Ethernet: Wi-Fi is the only network.
 *
 *   board:  RAM disk (512 MiB at 0x60000000, FAT32) -> radio up -> scan ->
 *           join (WPA2-PSK) -> DHCP -> file server on TCP 5000 + UDP beacon
 *           on 5001 ("MQPRO-RAM-DISK <ip> 5000") -> wait for the verdict
 *   PC:     wifi-ram-disk-host.py -- runs hw.sh (xfel), finds the board,
 *           sends files, lists the RAM disk, reads every file back as
 *           <file>.bak, compares SHA-256, sends "verdict PASS|FAIL"
 *   board:  the disk at the end (printed by the server), counters, verdict
 *           in the RTC, reset to FEL; hw.sh prints the RAM console log
 *
 * Memory: the image's window is 0x40000000..0x5fffffff (linker.ld, 512 MiB;
 * the console black box is at 0x58000000), so the disk takes the upper half
 * of the 1 GiB board.  The RAM disk's probe refuses a 512 MiB board, where
 * 0x60000000 would alias the image.
 *
 * Shared with the VisionFive 2 Lite tests (test/visionfive2-lite/
 * vf2-wifi-ram-disk/): ram-disk.cpp (the disk and FatFs's disk layer) and
 * file-server.cpp (the protocol); FatFs is the copy under that board's
 * emmc-fs-test/fatfs.  Everything else is this board's wifi-net stack.
 *
 * Threads:
 *
 *   net      above normal, 64 KiB, 2 ms poll.  OWNS the radio (SDIO, 802.11,
 *            WPA2), lwIP (NO_SYS) and the RAM disk's FatFs: every file
 *            operation runs in an lwIP callback on this thread, and the
 *            beacon is an lwIP timer, so one thread is the whole lock.
 *   os_main  normal.  Starts it, waits for the verdict (600 s watchdog),
 *            reports.
 *
 * The console (UART0) is the user's; hw.sh never opens it and reads the RAM
 * copy (the black box) over xfel after the test's reset to FEL.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <blackbox.hpp>
#include <board.hpp>
#include <hw_result.hpp>
#include <uart.hpp>

#include "console.hpp"
#include "net/net.hpp"
#include "platform.hpp"
#include "wifi/wlan.hpp"

#include "wifi-config.hpp"

#include "file-server.hpp"
#include "ram-disk.hpp"

#include <cstdint>

#if !defined(__riscv)
#error "wifi-ram-disk is a C906/RTL8723DS test"
#endif

using namespace os::rtos;

namespace
{
  constexpr unsigned watchdog_ms = 600 * 1000;

  alignas (16) std::uint8_t net_stack[64 * 1024];

  volatile bool g_done = false;
  volatile bool g_pass = false;
  const char* volatile g_failed_step = "RAM disk";

  unsigned
  now_ms (void)
  {
    return mqpro::platform::millis ();
  }

  // Poll the radio and the stack once (the file server runs inside lwIP).
  void
  service (void)
  {
    mqpro::wlan::poll ();
    mqpro::net::poll ();
  }

  bool
  bring_up (void)
  {
    g_failed_step = "RAM disk";
    if (!file_server::mount_disk ())
      {
        return false;
      }

    mqpro::serial.write ("\n[NET] bringing up the RTL8723DS ...\n");
    g_failed_step = "radio bring-up";
    if (!mqpro::wlan::start ())
      {
        return false;
      }
    mqpro::net::init ();

    g_failed_step = "file server";
    if (!file_server::start ("MQPRO-RAM-DISK"))
      {
        return false;
      }

    g_failed_step = "join";
    if (!wifi_test::configured)
      {
        mqpro::serial.write ("[NET] no network configured: build with "
                             "-DWIFI_TEST_SSID=... -DWIFI_TEST_PASS=...\n");
        return false;
      }
    // wlan::join scans for the SSID itself.
    bool joined = false;
    for (unsigned attempt = 1; attempt <= 3 && !joined; ++attempt)
      {
        mqpro::serial.print ("[NET] join \"{}\", attempt {} ...\n",
                             wifi_test::ssid, attempt);
        joined = mqpro::wlan::join (wifi_test::ssid, wifi_test::passphrase);
        if (!joined)
          {
            sysclock.sleep_for (1000);
          }
      }
    if (!joined)
      {
        return false;
      }

    g_failed_step = "DHCP";
    const unsigned start = now_ms ();
    while (!mqpro::net::has_address ())
      {
        service ();
        if (now_ms () - start >= 20000u)
          {
            mqpro::serial.write ("[NET] no DHCP lease within 20 s\n");
            return false;
          }
        sysclock.sleep_for (2);
      }
    mqpro::net::print_status ();
    mqpro::serial.print ("[TEST] READY: the PC can connect (TCP {}); waiting "
                         "for its verdict\n",
                         static_cast<unsigned> (file_server::tcp_port));
    g_failed_step = "file transfer (no verdict from the PC)";
    return true;
  }

  void*
  net_func (void*)
  {
    if (!bring_up ())
      {
        g_pass = false;
        g_done = true;
      }
    else
      {
        while (!file_server::verdict_received ())
          {
            service ();
            sysclock.sleep_for (2);
          }
        // Let the "OK" to the verdict and the PC's close go out.
        for (unsigned i = 0; i < 250; ++i)
          {
            service ();
            sysclock.sleep_for (2);
          }
        g_failed_step = file_server::verdict_pass ()
                            ? ""
                            : "the PC reported a failure";
        g_pass = file_server::verdict_pass ();
        g_done = true;
      }
    for (;;)
      {
        service ();
        sysclock.sleep_for (2);
      }
    return nullptr;
  }

  void
  print_counters (void)
  {
    const auto& w = mqpro::wlan::stats ();
    const file_server::counters& f = file_server::stats ();
    mqpro::serial.print (
        "[NET] wlan rx {} frames, {} delivered, crc {} decrypt {}, tx {} "
        "data {} errors\n",
        static_cast<unsigned> (w.rx_frames), static_cast<unsigned> (w.rx_data),
        static_cast<unsigned> (w.rx_crc_errors),
        static_cast<unsigned> (w.rx_decrypt_errors),
        static_cast<unsigned> (w.tx_data), static_cast<unsigned> (w.tx_errors));
    mqpro::serial.print (
        "[FILE] files in {} ({} B) out {} ({} B), {} connection(s), {} "
        "beacons\n",
        static_cast<unsigned> (f.files_received),
        static_cast<unsigned long long> (f.bytes_received),
        static_cast<unsigned> (f.files_sent),
        static_cast<unsigned long long> (f.bytes_sent),
        static_cast<unsigned> (f.connections),
        static_cast<unsigned> (f.beacons));
  }
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  uart::init (board::UART_BAUD);
  // The runner reads this RAM copy over xfel; the UART is the user's.
  mqpro::serial.set_mirror (&blackbox::put);

  mqpro::serial.write (
      "\n=== wifi-ram-disk: files over Wi-Fi into a 512 MiB RAM disk ===\n");
  mqpro::serial.print ("[TEST] ssid \"{}\"; file server TCP {}, beacon UDP "
                       "{}; watchdog {} s\n",
                       wifi_test::ssid,
                       static_cast<unsigned> (file_server::tcp_port),
                       static_cast<unsigned> (file_server::beacon_port),
                       watchdog_ms / 1000u);

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = net_stack;
  attr.th_stack_size_bytes = sizeof (net_stack);
  attr.th_priority = thread::priority::above_normal;
  static thread net_thread{ "net", net_func, nullptr, attr };

  const unsigned start = now_ms ();
  while (!g_done)
    {
      if (now_ms () - start >= watchdog_ms)
        {
          mqpro::serial.print ("\n[TEST] {} s without a verdict (stuck in: "
                               "{})\n",
                               watchdog_ms / 1000u, g_failed_step);
          print_counters ();
          hw_result::fail ();
        }
      sysclock.sleep_for (100);
    }

  print_counters ();
  if (g_pass)
    {
      mqpro::serial.print ("\n[TEST] PASS: {} file(s) stored on the RAM disk "
                           "and read back over Wi-Fi; PC: {}\n",
                           static_cast<unsigned> (
                               file_server::stats ().files_received),
                           file_server::verdict_text ());
      hw_result::ok ();
    }
  mqpro::serial.print ("\n[TEST] FAIL: {}{}{}\n", g_failed_step,
                       file_server::verdict_received () ? "; PC: " : "",
                       file_server::verdict_received ()
                           ? file_server::verdict_text ()
                           : "");
  hw_result::fail ();
}
