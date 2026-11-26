/*
 * wifi-net — the RTL8723DS 802.11 station and lwIP under the µOS++ scheduler.
 *
 * This is the bare-metal Wi-Fi application (the one in Documents/MangoPI)
 * re-hosted on the RTOS.  Nothing in the driver changed; what changed is
 * who runs it and when:
 *
 *   thread      prio        stack   period  role
 *   ----------  ----------  ------  ------  -------------------------------
 *   net         above_norm  32 KiB  2 ms    OWNS the radio: SDIO/SDC1, the
 *                                           802.11 state machine, lwIP.  Runs
 *                                           the scripted bring-up, join,
 *                                           DHCP wait and ping; polls the
 *                                           radio between steps.
 *   sd          normal       8 KiB  2 s     probes SDC0 (a different
 *                                           controller) and prints only when
 *                                           a card is present.
 *   os_main     normal         -      -     starts both, waits for the
 *                                           verdict, prints the summary.
 *
 * Why the analysis matters
 * -----------------------
 *   * ONE THREAD OWNS THE RADIO.  The RTL8723DS driver is a sequence of
 *     SDIO transactions with no locking of its own; two threads polling it
 *     would interleave CMD53 reads and corrupt frames.  The blocking helpers
 *     (`wlan::join`, `net::ping`) pump the radio internally, so they must run
 *     on that same thread too — which is why the script lives in net_func()
 *     rather than in os_main().
 *
 *   * THE POLL PERIOD IS 2 ms.  The chip's receive FIFO is small and the
 *     radio runs at 54 Mbit/s: a 1500-byte frame arrives in ~220 us.  At
 *     2 ms the FIFO never wraps during a burst, lwIP's timers (DHCP, ARP,
 *     TCP) are serviced at better than their 250 ms granularity, and the
 *     thread still sleeps 99% of the time.
 *
 *   * net IS ABOVE normal AND sd IS normal.  When the SD monitor is inside a
 *     card-identification timeout (hundreds of milliseconds with no
 *     preemption point) the radio must keep its cadence, so the radio thread
 *     must outrank it.  Nothing outranks the radio: there is no lower
 *     priority work that could delay a beacon.
 *
 *   * THE SD THREAD IS SAFE ALONGSIDE THE RADIO because it touches SDC0 and
 *     the radio is on SDC1: separate controllers, separate pin mux, no shared
 *     state.  The one shared resource is the console, and only the SD thread
 *     and the net thread print; a card announcement is one line and the
 *     console is character-polled, so the worst case is a single interleaved
 *     line, never a hang.
 *
 *   * plat_delay_us() SLEEPS.  Under the RTOS the driver's millisecond waits
 *     (power-up, calibration, EAPOL retries) go through the system clock
 *     instead of spinning, which is what lets the SD thread run at all while
 *     the radio powers up.  Sub-millisecond waits stay busy-waits because
 *     they are SDIO timing constraints.
 *
 * Verdict: PASS when the station joins, DHCP hands out an address, and an
 * ICMP echo to the gateway is answered.  Everything is printed as it happens;
 * the run ends with the port's RESULT line and a reset back to FEL.
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <blackbox.hpp>
#include <board.hpp>
#include <hw_result.hpp>
#include <uart.hpp>

#include "console.hpp"
#include "net/net.hpp"
#include "platform.hpp"
#include "sd/sd_card.hpp"
#include "wifi/wlan.hpp"

#include "wifi-config.hpp"

#include <cstdint>
#include <cstdio>

#if !defined(__riscv)
#error "wifi-net is a C906/RTL8723DS test"
#endif

using namespace os::rtos;

namespace
{
  // ---------------------------------------------------------------- threads

  alignas (16) std::uint8_t net_stack[32 * 1024];
  alignas (16) std::uint8_t sd_stack[8 * 1024];

  semaphore_binary g_net_done{ "net-done", 0 };
  volatile bool g_pass = false;

  // Set by the SD thread: a card was announced, so its removal re-arms the
  // "present" message (the test asks for a message when there is a card, not
  // a stream of them).
  volatile bool g_sd_announced = false;

  // The SD thread detects, the NET thread prints.  One thread owns the
  // console: the drivers print from the radio thread, and a second writer
  // would interleave mid-line ("[SD] card present: 2621[RTW] firmware ...").
  // Nothing here is a lock; it is the simpler rule that makes a lock
  // unnecessary.
  volatile bool g_sd_pending = false;
  volatile bool g_sd_removed = false;
  volatile std::uint32_t g_sd_blocks = 0;
  volatile std::uint32_t g_sd_clock = 0;
  volatile bool g_sd_hc = false;

  // Called on the radio thread only.
  void
  publish_sd (void)
  {
    if (!g_sd_pending)
      {
        return;
      }
    g_sd_pending = false;
    if (g_sd_removed)
      {
        g_sd_removed = false;
        mqpro::serial.write ("\n[SD] card removed\n");
        return;
      }
    mqpro::serial.print ("\n[SD] card present: {} blocks ({} MiB), {} Hz, {}\n",
                         static_cast<unsigned long> (g_sd_blocks),
                         static_cast<unsigned long> (g_sd_blocks / 2048u),
                         static_cast<unsigned> (g_sd_clock),
                         g_sd_hc ? "block addressing" : "byte addressing");
  }

  unsigned
  now_ms (void)
  {
    return mqpro::platform::millis ();
  }

  // Poll the radio and the stack once.  Only the net thread may call this.
  void
  service (void)
  {
    mqpro::wlan::poll ();
    mqpro::net::poll ();
    publish_sd ();
  }

  // ------------------------------------------------------------- SD monitor

  void*
  sd_func (void*)
  {
    for (;;)
      {
        const bool present = mqpro::sd::sdcard.init ();
        if (present)
          {
            if (!g_sd_announced)
              {
                // Publish the facts; the radio thread prints them, so the
                // console keeps one writer.
                const auto& info = mqpro::sd::sdcard.info ();
                g_sd_blocks = static_cast<std::uint32_t> (info.capacity_blocks);
                g_sd_clock = info.clock_hz;
                g_sd_hc = info.high_capacity;
                g_sd_pending = true;
                g_sd_announced = true;
              }
          }
        else if (g_sd_announced)
          {
            g_sd_removed = true;
            g_sd_announced = false;
          }

        sysclock.sleep_for (2000);
      }
    return nullptr;
  }

  // ------------------------------------------------------------- the radio

  void*
  net_func (void*)
  {
    mqpro::serial.write ("\n[NET] bringing up the RTL8723DS ...\n");
    if (!mqpro::wlan::start ())
      {
        mqpro::serial.write ("[NET] radio bring-up FAILED\n");
        g_pass = false;
        g_net_done.post ();
        for (;;)
          {
            sysclock.sleep_for (1000);
          }
      }

    mqpro::net::init ();
    publish_sd ();

    // --- scan -------------------------------------------------------------
    const auto networks = mqpro::wlan::scan ();
    mqpro::serial.print ("[NET] scan: {} network(s)\n",
                         static_cast<unsigned> (networks.size ()));
    bool target_seen = false;
    for (const auto& network : networks)
      {
        if (network.name () == wifi_test::ssid)
          {
            target_seen = true;
            mqpro::serial.print ("[NET] target \"{}\" ch {} {} dBm, {}\n",
                                 network.name (),
                                 static_cast<unsigned> (network.channel),
                                 static_cast<int> (network.rssi),
                                 network.security_text);
          }
      }
    if (!target_seen)
      {
        mqpro::serial.print ("[NET] target \"{}\" not in the scan\n",
                             wifi_test::ssid);
      }

    publish_sd ();

    // --- join -------------------------------------------------------------
    bool joined = false;
    if (wifi_test::configured)
      {
        for (unsigned attempt = 1; attempt <= 2 && !joined; ++attempt)
          {
            mqpro::serial.print ("[NET] join attempt {} ...\n", attempt);
            joined = mqpro::wlan::join (wifi_test::ssid, wifi_test::passphrase);
            if (!joined)
              {
                sysclock.sleep_for (1000);
              }
          }
      }

    publish_sd ();

    // --- DHCP -------------------------------------------------------------
    bool leased = false;
    if (joined)
      {
        const unsigned start = now_ms ();
        while (!(leased = mqpro::net::has_address ()))
          {
            service ();
            if (now_ms () - start >= 20000u)
              {
                break;
              }
            sysclock.sleep_for (2);
          }
        if (leased)
          {
            mqpro::net::print_status ();
          }
        else
          {
            mqpro::serial.write ("[NET] no DHCP lease within 20 s\n");
          }
      }

    publish_sd ();

    // --- ping -------------------------------------------------------------
    unsigned replies = 0;
    if (leased)
      {
        mqpro::serial.print ("[NET] ping gateway {} ...\n", wifi_test::gateway);
        replies = mqpro::net::ping (wifi_test::gateway, 4);
        if (wifi_test::peer[0] != '\0')
          {
            mqpro::serial.print ("[NET] ping peer {} ...\n", wifi_test::peer);
            (void)mqpro::net::ping (wifi_test::peer, 2);
          }
      }

    // --- counters ---------------------------------------------------------
    const auto& stat = mqpro::wlan::stats ();
    mqpro::serial.print ("[NET] rx {} frames, {} delivered, crc {} decrypt "
                         "{}, tx {} data {} errors\n",
                         static_cast<unsigned> (stat.rx_frames),
                         static_cast<unsigned> (stat.rx_data),
                         static_cast<unsigned> (stat.rx_crc_errors),
                         static_cast<unsigned> (stat.rx_decrypt_errors),
                         static_cast<unsigned> (stat.tx_data),
                         static_cast<unsigned> (stat.tx_errors));
    mqpro::serial.print ("[NET] threads: net above-normal 32 KiB 2 ms, sd "
                         "normal 8 KiB 2 s\n");

    g_pass = joined && leased && replies > 0;
    g_net_done.post ();

    // Keep the interface alive until os_main ends the run: this is the
    // "network is up" state of the test, and the counters above are the
    // useful evidence if anything looks marginal.
    for (;;)
      {
        service ();
        sysclock.sleep_for (2);
      }
    return nullptr;
  }

  // Print what the run is about to do, and why the threads are shaped the
  // way they are.
  void
  banner (void)
  {
    mqpro::serial.write ("\n=== wifi-net: RTL8723DS + lwIP on micro-os-plus ===\n");
    mqpro::serial.print ("[TEST] ssid \"{}\", gateway {}, peer \"{}\"\n",
                         wifi_test::ssid, wifi_test::gateway,
                         wifi_test::peer);
    mqpro::serial.write (
        "[TEST] threads: net (radio+lwIP, above normal, 32 KiB, 2 ms poll)\n"
        "[TEST]          sd  (SDC0 presence, normal, 8 KiB, 2 s probe)\n");
  }
} // namespace

// ---------------------------------------------------------------------------
// os_main: the test's own thread.  It starts the workers and waits for the
// radio thread to report; the run is bounded so a silent network fails
// instead of hanging the runner.

int
os_main (int /*argc*/, char* /*argv*/[])
{
  uart::init (board::UART_BAUD);

  // Mirror this console into the RAM log the runner reads over xfel: the
  // UART belongs to the user and a hanging Wi-Fi bring-up must still leave
  // evidence behind.
  mqpro::serial.set_mirror (&blackbox::put);

  banner ();

  thread::attributes attr = thread::initializer;

  // The SD monitor first: it must be running (and silent) while the radio
  // powers up, and it prints only when a card is actually there.
  attr.th_stack_address = sd_stack;
  attr.th_stack_size_bytes = sizeof (sd_stack);
  attr.th_priority = thread::priority::normal;
  static thread sd_thread{ "sd", sd_func, nullptr, attr };

  // The radio thread, one priority step above normal.
  attr.th_stack_address = net_stack;
  attr.th_stack_size_bytes = sizeof (net_stack);
  attr.th_priority = thread::priority::above_normal;
  static thread net_thread{ "net", net_func, nullptr, attr };

  // Bound the run: the radio thread signals when it has a verdict, and a
  // network that never answers must not leave the board wedged under xfel.
  constexpr unsigned kTimeoutMs = 90 * 1000;
  const unsigned start = now_ms ();
  while (g_net_done.try_wait () != result::ok)
    {
      if (now_ms () - start >= kTimeoutMs)
        {
          mqpro::serial.write ("\n[TEST] timeout waiting for the network\n");
          hw_result::fail ();
        }
      sysclock.sleep_for (100);
    }

  if (g_pass)
    {
      mqpro::serial.write ("\n[TEST] PASS: joined, leased, and the gateway "
                           "answered\n");
      hw_result::ok ();
    }

  mqpro::serial.write ("\n[TEST] FAIL: see the log above\n");
  hw_result::fail ();
}
