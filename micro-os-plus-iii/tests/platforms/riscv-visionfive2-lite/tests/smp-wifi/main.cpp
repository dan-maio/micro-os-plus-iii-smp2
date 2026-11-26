/*
 * smp-wifi (VisionFive 2 Lite)
 *
 * The board's on-board Wi-Fi -- an AIC8800D80/D40 FullMAC module on SDIO1 --
 * as an 802.11 station with lwIP on top, under the µOS++ SMP scheduler on
 * all four U74 harts.  The counterpart of the MangoPi MQ-Pro's wifi-net
 * (RTL8723DS, one C906), with the same script and the same verdict:
 *
 *   radio up -> scan -> join (WPA2-PSK or open) -> DHCP -> ping -> RESULT
 *
 * Threads (affinity in brackets):
 *
 *   wifi   [1]  above normal, 64 KiB.  OWNS the radio: SDIO1, the AIC8800
 *               message exchange, the WPA2 handshake and lwIP.  Runs the
 *               script above, then keeps the interface alive (2 ms poll).
 *   blink  [2]  normal.  The LED, twice a second, and a beat counter: the
 *               proof that a hart other than the radio's keeps its cadence
 *               while the radio thread busy-waits inside an SDIO transfer.
 *   stats  [3]  below normal.  Counters every 5 s, and the watchdog: no
 *               verdict within 180 s ends the run as a failure.
 *   os_main [0] starts the three, waits for the verdict, prints the summary.
 *
 * Why it is shaped this way
 * -------------------------
 *   * ONE THREAD OWNS THE RADIO.  The driver is a sequence of SDIO
 *     transactions with no locking of its own, and lwIP is built NO_SYS: two
 *     threads in either would interleave CMD53 transfers or corrupt a
 *     protocol control block.  The blocking helpers (wlan::join,
 *     wifi_net::ping) pump the radio internally, so the script runs on that
 *     same thread rather than in os_main().
 *
 *   * THE RADIO IS POLLED.  SDIO1's interrupt is not used: the thread reads
 *     the chip's status register every 2 ms and drains what is there (up to
 *     0x6F blocks per read).  At that cadence lwIP's timers are served far
 *     inside their 250 ms granularity and the thread still sleeps most of
 *     the time.
 *
 *   * THE WAITS SLEEP.  Between polls, and in every wait of the bring-up
 *     that is longer than a confirmation's round trip, the radio thread
 *     sleeps on the system clock -- which is what leaves its hart to the
 *     idle thread, and is why the other three threads are not needed to
 *     "make room" for it.
 *
 *   * THE CONSOLE HAS ONE WRITER AT A TIME.  Every thread prints through
 *     kprintf(), which takes one recursive mutex (net/wifi-lwip.cpp).
 *
 * The verdict: PASS when the station joined, DHCP leased an address and the
 * gateway answered an ICMP echo.  The build must name the network
 * (wifi-config.hpp); without it the radio still comes up and scans, and the
 * run FAILs at the join step, saying so.
 *
 * NOT YET PROVEN ON THE BOARD (see README.md).
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

#include <cstdint>

using namespace os::rtos;

namespace
{
  volatile bool run_done = false;
  volatile bool run_pass = false;
  volatile std::uint32_t blink_beats = 0;
  volatile unsigned wifi_cpu = 0xFF, blink_cpu = 0xFF, stats_cpu = 0xFF;

  const char* failed_step = "radio bring-up";

  void
  finish (bool pass)
  {
    run_pass = pass;
    run_done = true;
  }

  // Pin the calling thread to one CPU and wait until it runs there.
  void
  pin (unsigned cpu)
  {
    this_thread::thread ().cpu_affinity (1u << cpu);
    while (port_cpu_id () != cpu)
      {
        this_thread::yield ();
      }
  }

  // Poll the radio and the stack once.  Only the wifi thread calls this.
  void
  service (void)
  {
    wlan::poll ();
    wifi_net::poll ();
  }

  // ------------------------------------------------------------- the radio

  bool
  script (void)
  {
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

    // --- scan -------------------------------------------------------------
    failed_step = "scan";
    const auto networks = wlan::scan ();
    kprintf ("[wifi] scan: %u network(s)\n",
             static_cast<unsigned> (networks.size ()));
    bool target_seen = false;
    for (const auto& network : networks)
      {
        const bool is_target = wifi_test::configured
                               && network.name () == wifi_test::ssid;
        target_seen = target_seen || is_target;
        kprintf ("[wifi] %c %02x:%02x:%02x:%02x:%02x:%02x ch %3u %4d dBm  "
                 "%-22s \"%.*s\"\n",
                 is_target ? '*' : ' ', network.bssid[0], network.bssid[1],
                 network.bssid[2], network.bssid[3], network.bssid[4],
                 network.bssid[5], static_cast<unsigned> (network.channel),
                 static_cast<int> (network.rssi), network.security_text,
                 static_cast<int> (network.ssid_length), network.ssid.data ());
      }
    if (networks.empty ())
      {
        kprintf ("[wifi] the scan heard nothing (antenna? RF calibration?)\n");
        return false;
      }

    // --- join -------------------------------------------------------------
    failed_step = "join";
    if (!wifi_test::configured)
      {
        kprintf ("[wifi] no network configured: build with "
                 "-DWIFI_TEST_SSID=... -DWIFI_TEST_PASS=...\n");
        return false;
      }
    if (!target_seen)
      {
        kprintf ("[wifi] target \"%s\" not in the scan\n", wifi_test::ssid);
      }
    bool joined = false;
    for (unsigned attempt = 1; attempt <= 2 && !joined; ++attempt)
      {
        kprintf ("[wifi] join attempt %u ...\n", attempt);
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

    // --- DHCP -------------------------------------------------------------
    failed_step = "DHCP";
    if (!wifi_net::wait_for_address (20000))
      {
        kprintf ("[wifi] no DHCP lease within 20 s\n");
        wifi_net::print_status ();
        return false;
      }
    wifi_net::print_status ();

    // --- ping -------------------------------------------------------------
    failed_step = "ping";
    kprintf ("[wifi] ping gateway (%s) ...\n", wifi_test::gateway);
    const unsigned replies = wifi_net::ping (wifi_test::gateway, 4);
    kprintf ("[wifi] gateway replies: %u/4\n", replies);
    if (wifi_test::peer[0] != '\0')
      {
        kprintf ("[wifi] ping peer %s ...\n", wifi_test::peer);
        (void)wifi_net::ping (wifi_test::peer, 2);
      }
    wifi_net::print_status ();
    return replies > 0;
  }

  void*
  wifi_task (void*)
  {
    pin (1);
    wifi_cpu = port_cpu_id ();
    const bool pass = script ();
    if (pass)
      {
        failed_step = "";
      }
    finish (pass);

    // Keep the interface alive until os_main ends the run: the echo server
    // on port 7 answers for as long as the verdict takes to be reported.
    for (;;)
      {
        service ();
        sysclock.sleep_for (2);
      }
    return nullptr;
  }

  // ------------------------------------------------------------ the others

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
    kprintf ("[stats] sdio reads %u, rx data %u msg %u, tx data %u msg %u, "
             "busy %u, io errors %u, timeouts %u; wlan delivered %u dropped "
             "%u eapol %u tx %u/%u err; lwip rx %u tx %u drop %u; beats %u\n",
             static_cast<unsigned> (a.rx_reads),
             static_cast<unsigned> (a.rx_data),
             static_cast<unsigned> (a.rx_messages),
             static_cast<unsigned> (a.tx_data),
             static_cast<unsigned> (a.tx_messages),
             static_cast<unsigned> (a.tx_busy),
             static_cast<unsigned> (a.io_errors),
             static_cast<unsigned> (a.timeouts),
             static_cast<unsigned> (w.rx_delivered),
             static_cast<unsigned> (w.rx_dropped),
             static_cast<unsigned> (w.rx_eapol),
             static_cast<unsigned> (w.tx_data),
             static_cast<unsigned> (w.tx_errors),
             static_cast<unsigned> (n.rx_frames),
             static_cast<unsigned> (n.tx_frames),
             static_cast<unsigned> (n.rx_dropped),
             static_cast<unsigned> (blink_beats));
  }

  void*
  stats_task (void*)
  {
    pin (3);
    stats_cpu = port_cpu_id ();
    for (unsigned seconds = 5; seconds <= 180u; seconds += 5)
      {
        sysclock.sleep_for (5000);
        if (run_done)
          {
            return nullptr;
          }
        print_counters ();
      }
    kprintf ("[stats] 180 s without a verdict (stuck in: %s)\n", failed_step);
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
           "  smp-wifi : AIC8800 (SDIO1) + WPA2 + lwIP, 4 harts\n"
           "=============================================\n");
  kprintf ("[test] ssid \"%s\", %s, ping %s, peer \"%s\"\n", wifi_test::ssid,
           wifi_test::passphrase[0] != '\0' ? "WPA2-PSK" : "open",
           wifi_test::gateway, wifi_test::peer);
  kprintf ("[test] threads: wifi [cpu 1, above normal, 64 KiB, 2 ms poll], "
           "blink [cpu 2], stats+watchdog [cpu 3], os_main [cpu 0]\n");

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

  // The network stays up (the wifi thread keeps polling) while the summary
  // is printed.
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
  wifi_net::dump_transcript ();
  if (pass && spread)
    {
      kprintf ("\nRESULT: PASS  (joined \"%s\", DHCP lease, gateway "
               "answered)\n",
               wifi_test::ssid);
      hw_result::ok ();
    }
  else
    {
      kprintf ("\nRESULT: FAIL  (%s)\n",
               pass ? "thread placement" : failed_step);
      hw_result::fail ();
    }
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}
