/*
 * smp-net-ram-disk (VisionFive 2 Lite)
 *
 * Files over ETHERNET into a RAM disk, under the µOS++ SMP scheduler on all
 * four U74 harts.  The wired counterpart of vf2-wifi-ram-disk: no Wi-Fi at
 * all -- the network is the JH-7110's GMAC0 with the board's lwIP glue, as
 * in smp-test-usb-cdc-net (without its USB console).
 *
 *   board:  RAM disk (512 MiB at 0x60000000, FAT32) -> GMAC0 + PHY ->
 *           carrier -> DHCP -> file server on TCP 5000 + UDP beacon on 5001
 *           ("VF2-NET-RAM-DISK <ip> 5000") -> wait for the PC's verdict
 *   PC:     smp-net-ram-disk-host.py -- finds the board, sends files, lists
 *           the RAM disk, reads every file back as <file>.bak, compares
 *           SHA-256, sends "verdict PASS|FAIL"
 *   board:  the RAM disk at the end (printed by the server), counters, the
 *           console transcript over semihosting, RESULT, semihosting exit
 *
 * Shared with vf2-wifi-ram-disk: ram-disk.cpp (the disk and FatFs's disk
 * layer) and file-server.cpp (the protocol), from that test's directory.
 * Shared with smp-test-usb-cdc-net: gmac.cpp and net-lwip.cpp (the board's
 * net/ directory), unchanged.
 *
 * Threads (affinity in brackets):
 *
 *   eth-rx [1]  high.  Drains the GMAC receive ring into lwIP.  The file
 *               server's TCP callbacks -- and with them every FatFs call --
 *               run here, inside net_lwip::input().
 *   net    [2]  above normal.  lwIP timers (DHCP, TCP, the beacon) and the
 *               carrier (MDIO), every 10 ms.
 *   app    [3]  normal.  The script: carrier, DHCP, READY, wait for the
 *               verdict.
 *   stats  [0]  below normal.  Counters every 10 s.
 *   mon    [0]  low.  600 s watchdog.
 *   os_main[0]  starts them, blinks, waits for the verdict, reports.
 *
 * Why it is safe: lwIP (NO_SYS) is serialised by net-lwip.cpp's one lwIP
 * mutex -- input(), poll() and the transmit path all take it.  The file
 * server does nothing outside lwIP: its TCP callbacks, its beacon (an lwIP
 * timer) and the final listing (in the verdict handler) all run under that
 * mutex, so FatFs (FF_FS_REENTRANT 0) never sees two threads.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <board.hpp>
#include <gmac.hpp>
#include <led.hpp>
#include <net-lwip.hpp>
#include <riscv-smp.hpp>
#include <semihosting.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include "file-server.hpp"
#include "ram-disk.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

using namespace os::rtos;

namespace
{
  // Locally administered, distinct from smp-test-usb-cdc-net's ...:01 so
  // the router's DHCP and ARP tables keep the two tests apart.
  constexpr std::uint8_t kMac[6] = { 0x02, 0x56, 0x46, 0x4c, 0x33, 0x02 };

  constexpr unsigned watchdog_seconds = 600;

  volatile bool run_done = false;
  volatile bool dhcp_failed = false;
  volatile bool run_pass = false;
  volatile unsigned rx_cpu = 0xFF, net_cpu = 0xFF, app_cpu = 0xFF;
  const char* failed_step = "RAM disk";

  void
  log (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

  void
  log (const char* fmt, ...)
  {
    char b[320];
    va_list ap;
    va_start (ap, fmt);
    const int n = std::vsnprintf (b, sizeof (b), fmt, ap);
    va_end (ap);
    if (n > 0)
      {
        net_lwip::log (b);
      }
  }

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

#if defined(NET_RAMDISK_DHCP_TRACE)
  // Every DHCP frame (UDP port 67 or 68) the GMAC hands up, decoded before
  // lwIP sees it: does an OFFER/ACK arrive at all, and addressed to whom?
  void
  trace_dhcp_frame (const std::uint8_t* f, std::size_t n)
  {
    if (n < 14 + 20 + 8 + 240 || f[12] != 0x08 || f[13] != 0x00)
      {
        return;
      }
    const std::uint8_t* ip = f + 14;
    const std::size_t ihl = (ip[0] & 0x0fu) * 4u;
    if (ip[9] != 17 || n < 14 + ihl + 8 + 240)
      {
        return;
      }
    const std::uint8_t* udp = ip + ihl;
    const unsigned sport = (udp[0] << 8) | udp[1];
    const unsigned dport = (udp[2] << 8) | udp[3];
    if (!((sport == 67 || sport == 68) && (dport == 67 || dport == 68)))
      {
        return;
      }
    const std::uint8_t* d = udp + 8;
    unsigned type = 0;
    // options after the 236-byte fixed part and the 4-byte magic cookie
    for (std::size_t i = 240; i + 2 < n - 14 - ihl - 8;)
      {
        const unsigned code = d[i];
        if (code == 255)
          {
            break;
          }
        if (code == 0)
          {
            ++i;
            continue;
          }
        const unsigned len = d[i + 1];
        if (code == 53 && len >= 1)
          {
            type = d[i + 2];
          }
        i += 2 + len;
      }
    static const char* const names[]
        = { "?",    "DISCOVER", "OFFER", "REQUEST", "DECLINE",
            "ACK",  "NAK",      "RELEASE", "INFORM" };
    log ("[dhcp-rx] %s op %u xid %02x%02x%02x%02x flags %02x%02x "
         "%u.%u.%u.%u:%u -> %u.%u.%u.%u:%u  dst-mac "
         "%02x:%02x:%02x:%02x:%02x:%02x  chaddr "
         "%02x:%02x:%02x:%02x:%02x:%02x  yiaddr %u.%u.%u.%u  len %u\n",
         type < 9 ? names[type] : "?", d[0], d[4], d[5], d[6], d[7], d[10],
         d[11], ip[12], ip[13], ip[14], ip[15], sport, ip[16], ip[17], ip[18],
         ip[19], dport, f[0], f[1], f[2], f[3], f[4], f[5], d[28], d[29],
         d[30], d[31], d[32], d[33], d[16], d[17], d[18], d[19],
         static_cast<unsigned> (n));
  }
#endif

  // ---------------------------------------------------------------- threads

  void*
  rx_task (void*)
  {
    pin (1);
    rx_cpu = port_cpu_id ();
    static std::uint8_t frame[1600];
    for (;;)
      {
        int n;
        unsigned drained = 0;
        while ((n = gmac::poll (frame, sizeof (frame))) >= 0 && drained < 64u)
          {
#if defined(NET_RAMDISK_DHCP_TRACE)
            trace_dhcp_frame (frame, static_cast<std::size_t> (n));
#endif
            net_lwip::input (frame, static_cast<std::size_t> (n));
            ++drained;
          }
        if (drained == 0)
          {
            sysclock.sleep_for (1);
          }
      }
    return nullptr;
  }

  void*
  net_task (void*)
  {
    pin (2);
    net_cpu = port_cpu_id ();
    bool was_up = gmac::status ().link;
    for (;;)
      {
        net_lwip::poll ();
        const bool up = gmac::link_up (); // MDIO: this thread only
        if (up != was_up)
          {
            was_up = up;
            net_lwip::set_link (up);
            log ("[net] carrier %s (%u Mb/s %s duplex)\n",
                 up ? "up" : "lost",
                 static_cast<unsigned> (gmac::status ().speed_mbps),
                 gmac::status ().full_duplex ? "full" : "half");
          }
        sysclock.sleep_for (10);
      }
    return nullptr;
  }

  void*
  app_task (void*)
  {
    pin (3);
    app_cpu = port_cpu_id ();

    failed_step = "carrier";
    log ("[app] waiting for the carrier (10 s) ...\n");
    bool up = false;
    for (unsigned i = 0; i < 1000u && !up; ++i)
      {
        up = gmac::status ().link;
        if (!up)
          {
            sysclock.sleep_for (10);
          }
      }
    if (!up)
      {
        log ("[app] no ethernet link (cable? PHY?) phy id %04x:%04x bmsr "
             "0x%04x\n",
             static_cast<unsigned> (gmac::status ().phy_id1),
             static_cast<unsigned> (gmac::status ().phy_id2),
             static_cast<unsigned> (gmac::status ().bmsr));
        finish (false);
        return nullptr;
      }

    failed_step = "DHCP";
    if (!net_lwip::wait_for_address (15000))
      {
        log ("[app] no DHCP lease within 15 s\n");
        net_lwip::print_status ();
#if defined(NET_RAMDISK_FALLBACK_IP)
        // Diagnostics: carry on with a fixed address, so the link itself can
        // be tested (ping from the PC, small and large frames) in the same
        // run.  The verdict stays FAIL: DHCP is part of the test.
        dhcp_failed = true;
        file_server::request_static_address (NET_RAMDISK_FALLBACK_IP,
                                             0xffffff00u, 0xc0a80101u);
        while (!net_lwip::has_address ())
          {
            sysclock.sleep_for (100);
          }
        net_lwip::print_status ();
#else
        finish (false);
        return nullptr;
#endif
      }
    net_lwip::print_status ();
    log ("[app] READY: the PC can connect (TCP %u); waiting for its "
         "verdict\n",
         file_server::tcp_port);

    failed_step = "file transfer (no verdict from the PC)";
    while (!file_server::verdict_received ())
      {
        sysclock.sleep_for (20);
      }
    // Let the "OK" to the verdict and the PC's close go out.
    sysclock.sleep_for (500);
    failed_step = dhcp_failed ? "DHCP (the rest ran on the fallback address)"
                  : file_server::verdict_pass () ? ""
                                                 : "the PC reported a failure";
    finish (file_server::verdict_pass () && !dhcp_failed);
    return nullptr;
  }

  void
  print_counters (void)
  {
    const gmac::counters& g = gmac::stats ();
    const net_lwip::counters& n = net_lwip::stats ();
    const file_server::counters& f = file_server::stats ();
    log ("[stats] gmac rx %u tx %u err %u/%u busy %u unf %u ovf %u; lwip rx "
         "%u tx %u drop %u; files in %u (%llu B) out %u (%llu B), %u conn, "
         "%u beacons\n",
         static_cast<unsigned> (g.rx_frames),
         static_cast<unsigned> (g.tx_frames),
         static_cast<unsigned> (g.rx_errors),
         static_cast<unsigned> (g.tx_errors),
         static_cast<unsigned> (g.tx_busy),
         static_cast<unsigned> (g.tx_underflow),
         static_cast<unsigned> (g.rx_overflow),
         static_cast<unsigned> (n.rx_frames),
         static_cast<unsigned> (n.tx_frames),
         static_cast<unsigned> (n.rx_dropped),
         static_cast<unsigned> (f.files_received),
         static_cast<unsigned long long> (f.bytes_received),
         static_cast<unsigned> (f.files_sent),
         static_cast<unsigned long long> (f.bytes_sent),
         static_cast<unsigned> (f.connections),
         static_cast<unsigned> (f.beacons));
  }

  void*
  stats_task (void*)
  {
    for (;;)
      {
        sysclock.sleep_for (10000);
        if (!run_done)
          {
            print_counters ();
          }
      }
    return nullptr;
  }

  void*
  monitor_task (void*)
  {
    for (unsigned i = 0; i < watchdog_seconds; ++i)
      {
        sysclock.sleep_for (1000);
        if (run_done)
          {
            return nullptr;
          }
      }
    log ("[mon] %u s without a verdict (stuck in: %s)\n", watchdog_seconds,
         failed_step);
    finish (false);
    return nullptr;
  }

  // The whole console log to the debugger, so the hw.sh log holds the run
  // (UART0 stays the user's console; the board's uart.cpp forwards only the
  // RESULT line).  SYS_WRITE in 2 KiB pieces.
  void
  dump_transcript (void)
  {
#if defined(HW_BUILD) && defined(SEMIHOST)
    if (!semihosting::attached ())
      {
        return;
      }
    static char chunk[2048];
    const std::uint32_t total = net_lwip::log_length ();
    semihosting::write_buf ("\n---- console transcript ----\n", 30);
    for (std::uint32_t off = 0; off < total;)
      {
        const std::uint32_t n
            = total - off < sizeof (chunk) ? total - off : sizeof (chunk);
        net_lwip::log_copy (off, chunk, n);
        semihosting::write_buf (chunk, n);
        off += n;
      }
    semihosting::write_buf ("\n---- end of transcript ----\n", 29);
#endif
  }

  thread::stack::element_t rx_stack[8192];
  thread::stack::element_t net_stack[8192];
  thread::stack::element_t app_stack[8192];
  thread::stack::element_t stats_stack[4096];
  thread::stack::element_t mon_stack[4096];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::init (board::UART_BAUD);
  net_lwip::log_reset ();
  net_lwip::log ("\n=============================================\n");
  net_lwip::log ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  net_lwip::log ("  smp-net-ram-disk : files over ETHERNET into a 512 MiB "
                 "RAM disk\n");
  net_lwip::log ("=============================================\n");

  // The RAM disk first: nothing else runs yet, so FatFs needs no lock.
  if (!file_server::mount_disk ())
    {
      log ("\nRESULT: FAIL  (RAM disk)\n");
      hw_result::fail ();
    }

  failed_step = "GMAC";
  gmac::set_trace (&net_lwip::log);
  const bool gmac_ok = gmac::start (kMac);
  log ("[gmac] version 0x%04x, phy id 0x%04x:0x%04x, link %s %u Mb/s%s%s\n",
       static_cast<unsigned> (gmac::status ().version),
       static_cast<unsigned> (gmac::status ().phy_id1),
       static_cast<unsigned> (gmac::status ().phy_id2),
       gmac::status ().link ? "up" : "down",
       static_cast<unsigned> (gmac::status ().speed_mbps),
       gmac_ok ? "" : ", start failed: ", gmac_ok ? "" : gmac::last_error ());
  if (!gmac_ok)
    {
      log ("\nRESULT: FAIL  (GMAC start)\n");
      hw_result::fail ();
    }
  if (!net_lwip::init (kMac))
    {
      log ("\nRESULT: FAIL  (lwIP init)\n");
      hw_result::fail ();
    }
  // Still single-threaded: the server's pcbs and its beacon timer can be
  // created without the lwIP mutex.
  if (!file_server::start ("VF2-NET-RAM-DISK"))
    {
      log ("\nRESULT: FAIL  (file server)\n");
      hw_result::fail ();
    }
  if (gmac::status ().link)
    {
      net_lwip::set_link (true);
    }

  // Hold the creator above the workers while they are created (see
  // smp-test-usb-cdc-net: a higher-priority thread would otherwise start at
  // once and never yield back to this `normal` one).
  thread& self = this_thread::thread ();
  const thread::priority_t self_priority = self.priority ();
  self.priority (thread::priority::realtime);

  thread::attributes attr = thread::initializer;

  attr.th_stack_address = rx_stack;
  attr.th_stack_size_bytes = sizeof (rx_stack);
  attr.th_priority = thread::priority::high;
  static thread rx_th{ "eth-rx", rx_task, nullptr, attr };

  attr.th_stack_address = net_stack;
  attr.th_stack_size_bytes = sizeof (net_stack);
  attr.th_priority = thread::priority::above_normal;
  static thread net_th{ "net", net_task, nullptr, attr };

  attr.th_stack_address = app_stack;
  attr.th_stack_size_bytes = sizeof (app_stack);
  attr.th_priority = thread::priority::normal;
  static thread app_th{ "app", app_task, nullptr, attr };

  attr.th_stack_address = stats_stack;
  attr.th_stack_size_bytes = sizeof (stats_stack);
  attr.th_priority = thread::priority::below_normal;
  static thread stats_th{ "stats", stats_task, nullptr, attr };

  attr.th_stack_address = mon_stack;
  attr.th_stack_size_bytes = sizeof (mon_stack);
  attr.th_priority = thread::priority::low;
  static thread mon_th{ "mon", monitor_task, nullptr, attr };

  self.priority (self_priority);

  while (!run_done)
    {
      led::toggle ();
      sysclock.sleep_for (250);
    }

  const bool pass = run_pass;
  print_counters ();
  log ("[test] cpus: eth-rx %u, net %u, app %u, os_main %u\n", rx_cpu,
       net_cpu, app_cpu, port_cpu_id ());
  const bool spread = rx_cpu == 1 && net_cpu == 2 && app_cpu == 3;
  if (!spread)
    {
      log ("[test] the threads did not run on their own harts\n");
    }
  dump_transcript ();
  const file_server::counters& f = file_server::stats ();
  if (pass && spread)
    {
      log ("\nRESULT: PASS  (%u file(s) stored on the RAM disk and read back "
           "over Ethernet; PC: %s)\n",
           static_cast<unsigned> (f.files_received),
           file_server::verdict_text ());
      hw_result::ok ();
    }
  log ("\nRESULT: FAIL  (%s%s%s)\n", pass ? "thread placement" : failed_step,
       file_server::verdict_received () ? "; PC: " : "",
       file_server::verdict_received () ? file_server::verdict_text () : "");
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}
