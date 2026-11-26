/*
 * vf2-wifi-ram-disk (VisionFive 2 Lite)
 *
 * Files over Wi-Fi into a RAM disk, under the µOS++ SMP scheduler on all
 * four U74 harts.  The VisionFive 2 counterpart of the MangoPi MQ-Pro's
 * RAM-disk test, with the same wire protocol and the same PC tools:
 *
 *   board:  RAM disk (512 MiB at 0x60000000, FAT32) -> radio up (AIC8800 on
 *           SDIO1) -> scan -> join (WPA2-PSK) -> DHCP -> file server on
 *           TCP 5000 + UDP beacon on 5001 -> wait for the PC's verdict
 *   PC:     vf2-wifi-ram-disk-host.py -- finds the board by its beacon,
 *           sends files, lists the RAM disk, reads every file back as
 *           <file>.bak, compares SHA-256, sends "verdict PASS|FAIL"
 *   board:  prints the RAM disk, the counters, RESULT, semihosting exit
 *
 * The radio, WPA2 and lwIP code is smp-wifi's, unchanged (wifi/, net/ of
 * that directory); FatFs is the copy under emmc-fs-test/fatfs.  New here:
 * ram-disk.cpp (the disk and FatFs's disk layer) and file-server.cpp.
 *
 * Threads (affinity in brackets):
 *
 *   wifi   [1]  above normal, 64 KiB.  OWNS the radio, lwIP (NO_SYS) and the
 *               RAM disk's FatFs (FF_FS_REENTRANT 0): every file operation
 *               runs in an lwIP callback on this thread.  2 ms poll.
 *   blink  [2]  normal.  The LED and a beat counter (another hart keeps its
 *               cadence while the radio thread works).
 *   stats  [3]  below normal.  Counters every 10 s, and the watchdog: no
 *               verdict within 600 s ends the run as a failure.
 *   os_main [0] starts the three, waits for the verdict, prints the summary.
 *
 * The console (UART0) is the user's; the PC side never reads it.  At the
 * end the whole console transcript goes to the debugger in one semihosting
 * write (wifi_net::dump_transcript), so the hw.sh log holds the run.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <board.hpp>
#include <led.hpp>
#include <riscv-smp.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include "net/wifi-lwip.hpp"
#include "wifi/aic8800.hpp"
#include "wifi/wifi-platform.hpp"
#include "wifi/wlan.hpp"

#include "wifi-config.hpp"

#include "file-server.hpp"
#include "ram-disk.hpp"

#include <cstdint>

using namespace os::rtos;

namespace
{
  constexpr unsigned watchdog_seconds = 600;

  volatile bool run_done = false;
  volatile bool run_pass = false;
  volatile std::uint32_t blink_beats = 0;
  volatile unsigned wifi_cpu = 0xFF, blink_cpu = 0xFF, stats_cpu = 0xFF;

  const char* failed_step = "RAM disk";

  void
  finish (bool pass)
  {
    run_pass = pass;
    run_done = true;
  }

  void
  pin (unsigned cpu)
  {
    this_thread::thread ().cpu_affinity (1u << cpu);
    while (port_cpu_id () != cpu)
      {
        this_thread::yield ();
      }
  }

  void
  service (void)
  {
    wlan::poll ();
    wifi_net::poll ();
  }

  // Bring everything up; false = the step in failed_step failed.
  bool
  bring_up (void)
  {
    failed_step = "RAM disk";
    if (!file_server::mount_disk ())
      {
        return false;
      }

    kprintf ("\n[wifi] bringing up the AIC8800 on SDIO1 ...\n");
    failed_step = "radio bring-up";
    if (!wlan::start ())
      {
        return false;
      }
    failed_step = "lwIP";
    if (!wifi_net::init (wlan::address ().data ()))
      {
        return false;
      }
    failed_step = "file server";
    if (!file_server::start ("VF2-RAM-DISK"))
      {
        return false;
      }

    failed_step = "join";
    if (!wifi_test::configured)
      {
        kprintf ("[wifi] no network configured: build with "
                 "-DWIFI_TEST_SSID=... -DWIFI_TEST_PASS=...\n");
        return false;
      }
    // wlan::join scans for the SSID itself.
    bool joined = false;
    for (unsigned attempt = 1; attempt <= 3 && !joined; ++attempt)
      {
        kprintf ("[wifi] join \"%s\", attempt %u ...\n", wifi_test::ssid,
                 attempt);
        joined = wlan::join (wifi_test::ssid, wifi_test::passphrase);
        if (!joined)
          {
            sysclock.sleep_for (1000);
          }
      }
    if (!joined)
      {
        return false;
      }

    failed_step = "DHCP";
    if (!wifi_net::wait_for_address (20000))
      {
        kprintf ("[wifi] no DHCP lease within 20 s\n");
        wifi_net::print_status ();
        return false;
      }
    wifi_net::print_status ();
    kprintf ("[test] READY: the PC can connect (TCP %u); waiting for its "
             "verdict\n",
             file_server::tcp_port);
    failed_step = "file transfer (no verdict from the PC)";
    return true;
  }

  void*
  wifi_task (void*)
  {
    pin (1);
    wifi_cpu = port_cpu_id ();
    if (!bring_up ())
      {
        finish (false);
      }
    else
      {
        while (!file_server::verdict_received ())
          {
            service ();
            sysclock.sleep_for (2);
          }
        failed_step = file_server::verdict_pass ()
                          ? ""
                          : "the PC reported a failure";
        // Let the "OK" to the verdict and the PC's close go out.
        for (unsigned i = 0; i < 250; ++i)
          {
            service ();
            sysclock.sleep_for (2);
          }
        finish (file_server::verdict_pass ());
      }

    // Keep the interface alive while the summary is printed.
    for (;;)
      {
        service ();
        sysclock.sleep_for (2);
      }
    return nullptr;
  }

  void*
  blink_task (void*)
  {
    pin (2);
    blink_cpu = port_cpu_id ();
    for (;;)
      {
        led::toggle ();
        blink_beats = blink_beats + 1;
        sysclock.sleep_for (250);
      }
    return nullptr;
  }

  void
  print_counters (void)
  {
    const aic8800::counters& a = aic8800::stats ();
    const wlan::counters& w = wlan::stats ();
    const wifi_net::counters& n = wifi_net::stats ();
    const file_server::counters& f = file_server::stats ();
    kprintf ("[stats] sdio rx data %u tx data %u, io errors %u, timeouts %u; "
             "wlan delivered %u dropped %u; lwip rx %u tx %u drop %u; files "
             "in %u (%llu B) out %u (%llu B), %u conn, %u beacons; beats %u\n",
             static_cast<unsigned> (a.rx_data),
             static_cast<unsigned> (a.tx_data),
             static_cast<unsigned> (a.io_errors),
             static_cast<unsigned> (a.timeouts),
             static_cast<unsigned> (w.rx_delivered),
             static_cast<unsigned> (w.rx_dropped),
             static_cast<unsigned> (n.rx_frames),
             static_cast<unsigned> (n.tx_frames),
             static_cast<unsigned> (n.rx_dropped),
             static_cast<unsigned> (f.files_received),
             static_cast<unsigned long long> (f.bytes_received),
             static_cast<unsigned> (f.files_sent),
             static_cast<unsigned long long> (f.bytes_sent),
             static_cast<unsigned> (f.connections),
             static_cast<unsigned> (f.beacons),
             static_cast<unsigned> (blink_beats));
  }

  void*
  stats_task (void*)
  {
    pin (3);
    stats_cpu = port_cpu_id ();
    for (unsigned seconds = 10; seconds <= watchdog_seconds; seconds += 10)
      {
        sysclock.sleep_for (10000);
        if (run_done)
          {
            return nullptr;
          }
        print_counters ();
      }
    kprintf ("[stats] %u s without a verdict (stuck in: %s)\n",
             watchdog_seconds, failed_step);
    finish (false);
    return nullptr;
  }

  thread::stack::element_t wifi_stack[65536 / sizeof (thread::stack::element_t)];
  thread::stack::element_t blink_stack[8192 / sizeof (thread::stack::element_t)];
  thread::stack::element_t stats_stack[16384 / sizeof (thread::stack::element_t)];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::init (board::UART_BAUD);
  kprintf ("\n=============================================\n"
           "  " PORT_BANNER_LONG " - micro-os-plus-iii\n"
           "  vf2-wifi-ram-disk : files over Wi-Fi into a 512 MiB RAM disk\n"
           "=============================================\n");
  kprintf ("[test] ssid \"%s\", %s; file server TCP %u, beacon UDP %u; "
           "watchdog %u s\n",
           wifi_test::ssid,
           wifi_test::passphrase[0] != '\0' ? "WPA2-PSK" : "open",
           file_server::tcp_port, file_server::beacon_port,
           watchdog_seconds);

  thread::attributes attr = thread::initializer;

  attr.th_stack_address = wifi_stack;
  attr.th_stack_size_bytes = sizeof (wifi_stack);
  attr.th_priority = thread::priority::above_normal;
  static thread wifi_th{ "wifi", wifi_task, nullptr, attr };

  attr.th_stack_address = blink_stack;
  attr.th_stack_size_bytes = sizeof (blink_stack);
  attr.th_priority = thread::priority::normal;
  static thread blink_th{ "blink", blink_task, nullptr, attr };

  attr.th_stack_address = stats_stack;
  attr.th_stack_size_bytes = sizeof (stats_stack);
  attr.th_priority = thread::priority::below_normal;
  static thread stats_th{ "stats", stats_task, nullptr, attr };

  while (!run_done)
    {
      sysclock.sleep_for (100);
    }

  const bool pass = run_pass;
  print_counters ();
  kprintf ("[test] cpus: wifi %u, blink %u, stats %u, os_main %u; %u LED "
           "beats\n",
           wifi_cpu, blink_cpu, stats_cpu, port_cpu_id (),
           static_cast<unsigned> (blink_beats));
  const bool spread = wifi_cpu == 1 && blink_cpu == 2 && stats_cpu == 3
                      && blink_beats > 0;
  if (!spread)
    {
      kprintf ("[test] the threads did not run on their own harts\n");
    }
  const file_server::counters& f = file_server::stats ();
  wifi_net::dump_transcript ();
  if (pass && spread)
    {
      kprintf ("\nRESULT: PASS  (%u file(s) stored on the RAM disk and read "
               "back over Wi-Fi; PC: %s)\n",
               static_cast<unsigned> (f.files_received),
               file_server::verdict_text ());
      hw_result::ok ();
    }
  else
    {
      kprintf ("\nRESULT: FAIL  (%s%s%s)\n",
               pass ? "thread placement" : failed_step,
               file_server::verdict_received () ? "; PC: " : "",
               file_server::verdict_received ()
                   ? file_server::verdict_text ()
                   : "");
      hw_result::fail ();
    }
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}
