/*
 * wifi-lwip.cpp -- lwIP 2.2.1 over the AIC8800 station on the VisionFive 2
 * Lite (wifi-lwip.hpp).
 *
 * The board's Ethernet glue (test/boards/visionfive2-lite/net/net-lwip.cpp)
 * with the interface changed underneath: one netif ("wl0") whose output is
 * wlan::send_ethernet(), DHCP started when the Wi-Fi link comes up (keys
 * installed), an ICMP echo helper for the test and an RFC 862 echo server on
 * port 7.  The waits pump the radio, because the thread that waits is the
 * thread that owns it.
 *
 * It also carries what the Wi-Fi stack asks of its surroundings
 * (wifi/wifi-platform.hpp): the console writer and the clock.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "wifi-lwip.hpp"

#include "wifi/wifi-platform.hpp"
#include "wifi/wlan.hpp"

#include <uart.hpp>

#if defined(HW_BUILD) && defined(SEMIHOST)
#include <semihosting.hpp>
#endif

#include <lwip/dhcp.h>
#include <lwip/etharp.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/init.h>
#include <lwip/ip4.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/raw.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <netif/ethernet.h>

#include <cmsis-plus/rtos/os.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" std::uint32_t
sys_now (void)
{
  return static_cast<std::uint32_t> (os::rtos::sysclock.now ());
}

extern "C" void
kprintf (const char* format, ...)
{
  char b[512];
  va_list ap;
  va_start (ap, format);
  const int n = std::vsnprintf (b, sizeof (b), format, ap);
  va_end (ap);
  if (n > 0)
    {
      wifi_net::log (b);
    }
}

extern "C" unsigned int
net_random (void)
{
  static std::uint64_t state = 0;
  if (state == 0)
    {
      state = os::rtos::sysclock.now () * 0x9e3779b97f4a7c15ull
              + 0x2545f4914f6cdd1dull;
    }
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return static_cast<unsigned> ((state * 0x2545f4914f6cdd1dull) >> 32);
}

namespace wifi_platform
{
  std::uint32_t
  millis (void)
  {
    return static_cast<std::uint32_t> (os::rtos::sysclock.now ());
  }

  void
  sleep_ms (std::uint32_t ms)
  {
    os::rtos::sysclock.sleep_for (ms);
  }
} // namespace wifi_platform

namespace
{
  netif wl0{};
  bool initialised = false;
  bool dhcp_started = false;
  wifi_net::counters cnt{};

  alignas (8) std::uint8_t tx_frame[1600];

  // One writer for UART0 across the whole test: the radio, the net code, the
  // other threads and lwIP's own diagnostics all end here.
  os::rtos::mutex_recursive console_mutex{"console"};

  // Everything printed is also kept here: UART0 is the console, but the
  // hardware runner (hw.sh) sees only what crosses JTAG, and copying every
  // write to semihosting halts the hart for a probe round trip each time.
  // The transcript goes over in ONE semihosting write when the run ends
  // (wifi_net::dump_transcript ()).
  char transcript[48 * 1024];
  std::size_t transcript_used = 0;

  void
  log (const char* s)
  {
    console_mutex.lock ();
    const std::size_t n = std::strlen (s);
    const std::size_t room = sizeof (transcript) - transcript_used;
    const std::size_t take = n < room ? n : room;
    std::memcpy (transcript + transcript_used, s, take);
    transcript_used += take;
    uart::write (s);
    console_mutex.unlock ();
  }

  // "address a.b.c.d mask ... gateway ..." of the interface, into `out`.
  void
  format_addresses (netif* itf, char* out, std::size_t size)
  {
    const ip4_addr_t* a = netif_ip4_addr (itf);
    const ip4_addr_t* m = netif_ip4_netmask (itf);
    const ip4_addr_t* g = netif_ip4_gw (itf);
    std::snprintf (out, size,
                   "address %u.%u.%u.%u mask %u.%u.%u.%u gateway %u.%u.%u.%u",
                   ip4_addr1 (a), ip4_addr2 (a), ip4_addr3 (a), ip4_addr4 (a),
                   ip4_addr1 (m), ip4_addr2 (m), ip4_addr3 (m), ip4_addr4 (m),
                   ip4_addr1 (g), ip4_addr2 (g), ip4_addr3 (g), ip4_addr4 (g));
  }

  void
  on_status (netif* itf)
  {
    if (ip4_addr_isany_val (*netif_ip4_addr (itf)))
      {
        return;
      }
    char where[96];
    char line[128];
    format_addresses (itf, where, sizeof (where));
    std::snprintf (line, sizeof (line), "[net] lease: %s\n", where);
    log (line);
  }

  // ------------------------------------------------------------- echo server

  struct echo_conn
  {
    tcp_pcb* pcb{nullptr};
    pbuf* pending{nullptr};
    std::uint64_t bytes{0};
    bool closing{false};
    bool used{false};
  };

  echo_conn echo_conns[4];

  void
  echo_release (echo_conn& c)
  {
    if (c.pending != nullptr)
      {
        pbuf_free (c.pending);
        c.pending = nullptr;
      }
    if (c.pcb != nullptr)
      {
        tcp_arg (c.pcb, nullptr);
        tcp_recv (c.pcb, nullptr);
        tcp_sent (c.pcb, nullptr);
        tcp_err (c.pcb, nullptr);
        tcp_close (c.pcb);
        c.pcb = nullptr;
      }
    c.used = false;
    c.closing = false;
    c.bytes = 0;
  }

  void
  echo_push (echo_conn& c)
  {
    while (c.pending != nullptr)
      {
        const std::uint16_t room = tcp_sndbuf (c.pcb);
        if (room == 0)
          {
            return;
          }
        const std::uint16_t chunk
            = (c.pending->len < room) ? c.pending->len : room;
        const err_t e = tcp_write (c.pcb, c.pending->payload, chunk,
                                   TCP_WRITE_FLAG_COPY);
        if (e != ERR_OK)
          {
            return;
          }
        pbuf* rest = c.pending;
        if (chunk >= c.pending->len)
          {
            c.pending = c.pending->next;
            if (c.pending != nullptr)
              {
                pbuf_ref (c.pending);
              }
          }
        pbuf_free (rest);
        c.bytes += chunk;
        tcp_output (c.pcb);
        (void)tcp_recved (c.pcb, chunk);
        cnt.echo_bytes += chunk;
      }
    if (c.closing)
      {
        tcp_output (c.pcb);
        tcp_close (c.pcb);
        c.pcb = nullptr;
        c.used = false;
      }
  }

  err_t
  echo_sent (void* arg, tcp_pcb*, std::uint16_t)
  {
    echo_push (*static_cast<echo_conn*> (arg));
    return ERR_OK;
  }

  err_t
  echo_recv (void* arg, tcp_pcb*, pbuf* p, err_t err)
  {
    echo_conn& c = *static_cast<echo_conn*> (arg);
    if (p == nullptr || err != ERR_OK)
      {
        if (c.pcb != nullptr)
          {
            tcp_close (c.pcb);
            c.pcb = nullptr;
          }
        return ERR_OK;
      }
    // Queue the payload behind anything still unsent.
    if (c.pending == nullptr)
      {
        c.pending = p;
      }
    else
      {
        pbuf_cat (c.pending, p);
      }
    echo_push (c);
    return ERR_OK;
  }

  void
  echo_err (void* arg, err_t)
  {
    echo_conn& c = *static_cast<echo_conn*> (arg);
    c.pcb = nullptr; // lwIP has already freed it
    if (c.pending != nullptr)
      {
        pbuf_free (c.pending);
        c.pending = nullptr;
      }
    c.used = false;
  }

  err_t
  echo_accept (void* arg, tcp_pcb* pcb, err_t)
  {
    (void)arg;
    for (echo_conn& c : echo_conns)
      {
        if (!c.used)
          {
            c.used = true;
            c.pcb = pcb;
            c.bytes = 0;
            c.closing = false;
            tcp_arg (pcb, &c);
            tcp_recv (pcb, echo_recv);
            tcp_sent (pcb, echo_sent);
            tcp_err (pcb, echo_err);
            tcp_nagle_disable (pcb);
            ++cnt.echo_connections;
            log ("[net] echo: connection\n");
            return ERR_OK;
          }
      }
    return ERR_MEM;
  }

  void
  echo_listen (void)
  {
    tcp_pcb* pcb = tcp_new_ip_type (IPADDR_TYPE_V4);
    if (pcb == nullptr)
      {
        return;
      }
    if (tcp_bind (pcb, IP_ANY_TYPE, wifi_net::echo_port) != ERR_OK)
      {
        tcp_close (pcb);
        return;
      }
    tcp_pcb* l = tcp_listen (pcb);
    if (l == nullptr)
      {
        tcp_close (pcb);
        return;
      }
    tcp_accept (l, echo_accept);
  }

  // -------------------------------------------------------------------- ping

  struct ping_state
  {
    volatile bool replied{false};
    volatile std::uint32_t rtt_us{0};
    std::uint16_t seq{0};
    std::uint8_t ttl{0};
    std::uint16_t length{0};
  };

  ping_state pinging{};

  u8_t
  ping_received (void*, raw_pcb*, pbuf* p, const ip_addr_t*)
  {
    if (p == nullptr || p->tot_len < static_cast<std::uint16_t> (IP_HLEN + 8))
      {
        return 0;
      }
    std::uint8_t head[IP_HLEN + 8];
    pbuf_copy_partial (p, head, sizeof (head), 0);
    const std::uint16_t id = static_cast<std::uint16_t> (
        (head[IP_HLEN + 4] << 8) | head[IP_HLEN + 5]);
    if (id != 0x4d51u)
      {
        return 0; // not ours
      }
    pinging.ttl = head[8];
    pinging.replied = true;
    pbuf_free (p);
    return 1;
  }

  // -------------------------------------------------------------------- glue

  // Called by the wlan layer, on the thread that owns it.
  void
  on_frame (std::span<const std::uint8_t> frame)
  {
    wifi_net::input (frame.data (), frame.size ());
  }

  void
  on_link (bool up)
  {
    wifi_net::set_link (up);
  }

  err_t
  link_output (netif*, pbuf* p)
  {
    if (p->tot_len > sizeof (tx_frame))
      {
        return ERR_BUF;
      }
    pbuf_copy_partial (p, tx_frame, p->tot_len, 0);
    ++cnt.tx_frames;
    if (!wlan::send_ethernet ({ tx_frame, p->tot_len }))
      {
        return ERR_IF;
      }
    return ERR_OK;
  }

  err_t
  interface_init (netif* itf)
  {
    itf->name[0] = 'w';
    itf->name[1] = 'l';
    itf->output = etharp_output;
    itf->linkoutput = link_output;
    itf->mtu = 1500;
    itf->hwaddr_len = ETH_HWADDR_LEN;
    itf->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
  }

  template <typename Predicate>
  bool
  wait_for (std::uint32_t timeout_ms, Predicate done)
  {
    const std::uint32_t start = static_cast<std::uint32_t> (
        os::rtos::sysclock.now ());
    for (;;)
      {
        wlan::poll ();
        wifi_net::poll ();
        if (done ())
          {
            return true;
          }
        if (static_cast<std::uint32_t> (os::rtos::sysclock.now ()) - start
            >= timeout_ms)
          {
            return false;
          }
        os::rtos::sysclock.sleep_for (1);
      }
  }
} // namespace

namespace wifi_net
{
  void
  log (const char* text)
  {
    ::log (text); // the single-writer helper above
  }

  void
  dump_transcript (void)
  {
#if defined(HW_BUILD) && defined(SEMIHOST)
    console_mutex.lock ();
    static constexpr char head[] = "\n----- console transcript -----\n";
    static constexpr char tail[] = "----- end of transcript -----\n";
    semihosting::write_buf (head, sizeof (head) - 1);
    semihosting::write_buf (transcript, transcript_used);
    semihosting::write_buf (tail, sizeof (tail) - 1);
    console_mutex.unlock ();
#endif
  }

  const counters&
  stats (void)
  {
    return cnt;
  }

  bool
  init (const std::uint8_t* mac)
  {
    if (initialised)
      {
        return true;
      }
    lwip_init ();
    netif_add (&wl0, nullptr, nullptr, nullptr, nullptr, interface_init,
               ethernet_input);
    netif_set_default (&wl0);
    netif_set_status_callback (&wl0, on_status);
    if (mac != nullptr)
      {
        std::memcpy (wl0.hwaddr, mac, ETH_HWADDR_LEN);
      }
    netif_set_hostname (&wl0, "vf2-lite");
    netif_set_up (&wl0);
    wlan::set_receive_handler (on_frame);
    wlan::set_link_handler (on_link);
    echo_listen ();
    initialised = true;
    return true;
  }

  void
  poll (void)
  {
    if (initialised)
      {
        sys_check_timeouts ();
      }
  }

  void
  input (const std::uint8_t* frame, std::size_t length)
  {
    if (!initialised || frame == nullptr)
      {
        return;
      }
    pbuf* p = pbuf_alloc (PBUF_RAW, static_cast<u16_t> (length), PBUF_POOL);
    if (p == nullptr)
      {
        ++cnt.rx_dropped;
        return;
      }
    pbuf_take (p, frame, static_cast<u16_t> (length));
    ++cnt.rx_frames;
    if (wl0.input (p, &wl0) != ERR_OK)
      {
        pbuf_free (p);
      }
  }

  void
  set_link (bool up)
  {
    if (!initialised)
      {
        return;
      }
    if (up)
      {
        netif_set_link_up (&wl0);
        if (!dhcp_started)
          {
            dhcp_started = true;
            dhcp_start (&wl0);
          }
      }
    else
      {
        netif_set_link_down (&wl0);
      }
  }

  bool
  link (void)
  {
    return initialised && netif_is_link_up (&wl0);
  }

  bool
  has_address (void)
  {
    return initialised && !ip4_addr_isany_val (*netif_ip4_addr (&wl0));
  }

  bool
  wait_for_address (std::uint32_t timeout_ms)
  {
    return wait_for (timeout_ms, [] { return has_address (); });
  }

  void
  print_status (void)
  {
    if (!initialised)
      {
        log ("[net] not initialised\n");
        return;
      }
    char where[96];
    char b[256];
    format_addresses (&wl0, where, sizeof (where));
    std::snprintf (b, sizeof (b),
                   "[net] wl0 link %s, %s\n[net] frames rx %u tx %u dropped "
                   "%u; echo %u conn, %lu bytes\n",
                   netif_is_link_up (&wl0) ? "up" : "down", where,
                   static_cast<unsigned> (cnt.rx_frames),
                   static_cast<unsigned> (cnt.tx_frames),
                   static_cast<unsigned> (cnt.rx_dropped),
                   static_cast<unsigned> (cnt.echo_connections),
                   static_cast<unsigned long> (cnt.echo_bytes));
    log (b);
  }

  unsigned
  ping (std::string_view target, unsigned count)
  {
    if (!has_address ())
      {
        log ("[net] ping: no address\n");
        return 0;
      }
    ip_addr_t destination{};
    if (target == "gw")
      {
        destination = *netif_ip4_gw (&wl0);
      }
    else
      {
        char text[32];
        const std::size_t n = (target.size () < sizeof (text) - 1)
                                  ? target.size ()
                                  : sizeof (text) - 1;
        std::memcpy (text, target.data (), n);
        text[n] = '\0';
        if (ipaddr_aton (text, &destination) == 0)
          {
            log ("[net] ping: bad address\n");
            return 0;
          }
      }

    raw_pcb* pcb = raw_new (IP_PROTO_ICMP);
    if (pcb == nullptr)
      {
        return 0;
      }
    raw_recv (pcb, ping_received, nullptr);
    raw_bind (pcb, IP_ADDR_ANY);

    unsigned replies = 0;
    for (unsigned i = 0; i < count; ++i)
      {
        pinging.replied = false;
        pinging.seq = static_cast<std::uint16_t> (i);
        pinging.length = 56;

        pbuf* p = pbuf_alloc (PBUF_IP, 56, PBUF_RAM);
        if (p == nullptr)
          {
            break;
          }
        auto* echo = static_cast<icmp_echo_hdr*> (p->payload);
        ICMPH_TYPE_SET (echo, ICMP_ECHO);
        ICMPH_CODE_SET (echo, 0);
        echo->id = lwip_htons (0x4d51u);
        echo->seqno = lwip_htons (pinging.seq);
        for (std::uint16_t k = 8; k < 56; ++k)
          {
            static_cast<std::uint8_t*> (p->payload)[k]
                = static_cast<std::uint8_t> (k + i);
          }
        // lwIP fills the checksum when IP_HLEN is in front of the ICMP header
        // only if we ask; compute it here over the ICMP bytes.
        echo->chksum = 0;
        echo->chksum = inet_chksum (p->payload, 56);

        const std::uint32_t t0 = static_cast<std::uint32_t> (
            os::rtos::sysclock.now ());
        if (raw_sendto (pcb, p, &destination) == ERR_OK)
          {
            if (wait_for (1000, [] { return pinging.replied; }))
              {
                ++replies;
                pinging.rtt_us = (static_cast<std::uint32_t> (
                                      os::rtos::sysclock.now ())
                                  - t0)
                                 * 1000u;
                char b[96];
                std::snprintf (b, sizeof (b),
                               "[net] 64 bytes from %u.%u.%u.%u: icmp_seq=%u "
                               "ttl=%u time=%lu.%03lu ms\n",
                               ip4_addr1 (&destination), ip4_addr2 (&destination),
                               ip4_addr3 (&destination), ip4_addr4 (&destination),
                               i + 1, static_cast<unsigned> (pinging.ttl),
                               static_cast<unsigned long> (pinging.rtt_us / 1000),
                               static_cast<unsigned long> (pinging.rtt_us % 1000));
                log (b);
              }
            else
              {
                char b[64];
                std::snprintf (b, sizeof (b),
                               "[net] icmp_seq=%u: no reply within 1 s\n",
                               i + 1);
                log (b);
              }
          }
        pbuf_free (p);
        os::rtos::sysclock.sleep_for (500);
      }
    raw_remove (pcb);
    return replies;
  }
} // namespace wifi_net
