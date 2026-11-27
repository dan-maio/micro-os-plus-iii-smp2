/*
 * net-lwip.cpp -- lwIP 2.2.1 over GMAC0 on the VisionFive 2 Lite.
 *
 * The glue is deliberately small: one netif, DHCP started when the carrier
 * comes up, an ICMP echo helper for the test, and an RFC 862 echo server on
 * port 7 so a host on the LAN can prove the receive path too.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <net-lwip.hpp>

#include <board.hpp>
#include <gmac.hpp>
#include <uart.hpp>

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

// The JH-7110's L2 controller (sifive_ccache.c): write back / invalidate a
// range, line by line.  The RAM log must reach DDR, or a JTAG dump reads the
// stale array instead of what the test wrote.
extern "C" void vf2_dcache_flush (const void* addr, std::uint32_t len);

extern "C" std::uint32_t
sys_now (void)
{
  return static_cast<std::uint32_t> (os::rtos::sysclock.now ());
}

extern "C" void
kprintf (const char* format, ...)
{
  char b[192];
  va_list ap;
  va_start (ap, format);
  const int n = std::vsnprintf (b, sizeof (b), format, ap);
  va_end (ap);
  if (n > 0)
    {
      net_lwip::log (b);
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

namespace
{
  netif et0{};
  bool initialised = false;
  bool dhcp_started = false;
  net_lwip::counters cnt{};

  alignas (8) std::uint8_t tx_frame[1600];

  // One writer for UART0 across the whole test: the net code, the app, and
  // lwIP's own diagnostics all end here.
  os::rtos::mutex_recursive console_mutex{"console"};

  // lwIP is built with NO_SYS=1 (a single-threaded core): every entry point
  // must be serialised.  Here three threads use it -- the receive thread
  // (input), the net thread (timers) and the app (ping, DHCP poll) -- so a
  // recursive lock guards the core.  It is held for individual calls only,
  // never across a sleep: otherwise the receive thread could not deliver the
  // frame the caller is waiting for.
  os::rtos::mutex_recursive lwip_mutex{"lwip"};

  struct lwip_lock
  {
    lwip_lock () { lwip_mutex.lock (); }
    ~lwip_lock () { lwip_mutex.unlock (); }
    lwip_lock (const lwip_lock&) = delete;
    lwip_lock&
    operator= (const lwip_lock&)
        = delete;
  };

  // The same text is kept in RAM at board::SCRATCH_BASE, so a run can be
  // read back over JTAG (OpenOCD) without touching the serial port.  Layout:
  // magic, length, text -- the text is NUL-terminated as it is written.
  constexpr std::uintptr_t LOG_BASE = board::SCRATCH_BASE;
  constexpr std::uint32_t LOG_MAGIC = 0x5646324Cu; // "VF2L"
  constexpr std::uint32_t LOG_CAPACITY = 48u * 1024u;

  inline volatile std::uint32_t&
  log_word (std::uint32_t index)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (LOG_BASE + 4u * index);
  }

  inline volatile char*
  log_text (void)
  {
    return reinterpret_cast<volatile char*> (LOG_BASE + 8u);
  }

  void
  log_to_ram (char c)
  {
    const std::uint32_t n = log_word (1);
    if (n + 1u < LOG_CAPACITY)
      {
        log_text ()[n] = c;
        log_text ()[n + 1u] = '\0';
        __asm__ volatile("" ::: "memory");
        log_word (1) = n + 1u;
      }
  }

  void
  log (const char* s)
  {
    console_mutex.lock ();
    for (const char* p = s; *p != '\0'; ++p)
      {
        log_to_ram (*p);
      }
    // One cache write-back per line: the log is meant to be read through the
    // bus (JTAG), which does not see dirty cache lines.
    vf2_dcache_flush (reinterpret_cast<const void*> (LOG_BASE),
                      static_cast<std::uint32_t> (8u + log_word (1) + 1u));
    uart::write (s);
    console_mutex.unlock ();
  }

  void
  print_ip (char* out, std::size_t length, const ip4_addr_t& a)
  {
    std::snprintf (out, length, "%u.%u.%u.%u", ip4_addr1 (&a), ip4_addr2 (&a),
                   ip4_addr3 (&a), ip4_addr4 (&a));
  }

  void
  on_status (netif* itf)
  {
    if (ip4_addr_isany_val (*netif_ip4_addr (itf)))
      {
        return;
      }
    char address[24];
    char mask[24];
    char gateway[24];
    print_ip (address, sizeof (address), *netif_ip4_addr (itf));
    print_ip (mask, sizeof (mask), *netif_ip4_netmask (itf));
    print_ip (gateway, sizeof (gateway), *netif_ip4_gw (itf));
    char b[128];
    std::snprintf (b, sizeof (b), "\n[net] lease: address %s mask %s gateway %s\n",
                   address, mask, gateway);
    log (b);

    // Announce the new binding: until the peer's ARP cache has our MAC, an
    // echo request is delivered to the IP stack only to be dropped (or its
    // reply is sent to an unknown MAC).  A gratuitous ARP here makes the
    // first ping after the lease reliable.
    {
      lwip_lock guard;
      etharp_gratuitous (&et0);
    }
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
    if (tcp_bind (pcb, IP_ANY_TYPE, net_lwip::echo_port) != ERR_OK)
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

  err_t
  link_output (netif*, pbuf* p)
  {
    if (p->tot_len > sizeof (tx_frame))
      {
        return ERR_BUF;
      }
    pbuf_copy_partial (p, tx_frame, p->tot_len, 0);
    ++cnt.tx_frames;
    if (!gmac::send (tx_frame, p->tot_len))
      {
        return ERR_IF;
      }
    return ERR_OK;
  }

  err_t
  interface_init (netif* itf)
  {
    itf->name[0] = 'e';
    itf->name[1] = 't';
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
        net_lwip::poll ();
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

namespace net_lwip
{
  void
  log (const char* text)
  {
    ::log (text); // the single-writer helper above
  }

  void
  log_reset (void)
  {
    log_word (0) = LOG_MAGIC;
    log_word (1) = 0;
    log_text ()[0] = '\0';
  }

  std::uint32_t
  log_length (void)
  {
    return log_word (1);
  }

  void
  log_copy (std::uint32_t offset, char* out, std::uint32_t length)
  {
    const std::uint32_t total = log_word (1);
    if (offset >= total)
      {
        return;
      }
    const std::uint32_t n = (length < total - offset) ? length : total - offset;
    for (std::uint32_t i = 0; i < n; ++i)
      {
        out[i] = log_text ()[offset + i];
      }
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
    // Claim the RAM log if nobody has: never clear it, the lines before
    // lwIP came up (banner, GMAC bring-up) are the interesting ones.
    if (log_word (0) != LOG_MAGIC)
      {
        log_word (0) = LOG_MAGIC;
        log_word (1) = 0;
        log_text ()[0] = '\0';
      }

    lwip_init ();
    netif_add (&et0, nullptr, nullptr, nullptr, nullptr, interface_init,
               ethernet_input);
    netif_set_default (&et0);
    netif_set_status_callback (&et0, on_status);
    if (mac != nullptr)
      {
        std::memcpy (et0.hwaddr, mac, ETH_HWADDR_LEN);
      }
    netif_set_up (&et0);
    echo_listen ();
    initialised = true;
    return true;
  }

  void
  poll (void)
  {
    if (initialised)
      {
        lwip_lock guard;
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
    lwip_lock guard;
    pbuf* p = pbuf_alloc (PBUF_RAW, static_cast<u16_t> (length), PBUF_POOL);
    if (p == nullptr)
      {
        ++cnt.rx_dropped;
        return;
      }
    pbuf_take (p, frame, static_cast<u16_t> (length));
    ++cnt.rx_frames;
    if (et0.input (p, &et0) != ERR_OK)
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
        netif_set_link_up (&et0);
        if (!dhcp_started)
          {
            dhcp_started = true;
            dhcp_start (&et0);
          }
      }
    else
      {
        dhcp_stop (&et0);
        dhcp_started = false;
        netif_set_link_down (&et0);
      }
  }

  bool
  link (void)
  {
    return initialised && netif_is_link_up (&et0);
  }

  bool
  has_address (void)
  {
    return initialised && !ip4_addr_isany_val (*netif_ip4_addr (&et0));
  }

  bool
  wait_for_address (std::uint32_t timeout_ms)
  {
    return wait_for (timeout_ms, [] { return has_address (); });
  }

  void
  dhcp_restart (void)
  {
    if (!initialised || !netif_is_link_up (&et0))
      {
        return;
      }
    lwip_lock l;
    dhcp_stop (&et0);
    dhcp_start (&et0);
    dhcp_started = true;
  }

  bool
  wait_for_address (std::uint32_t timeout_ms, unsigned attempts)
  {
    for (unsigned a = 1; a <= attempts; ++a)
      {
        if (wait_for_address (timeout_ms))
          {
            return true;
          }
        if (a < attempts)
          {
            kprintf ("[net] no DHCP lease in %u ms (attempt %u of %u): "
                 "restarting DHCP\n",
                 static_cast<unsigned> (timeout_ms), a, attempts);
            dhcp_restart ();
          }
      }
    return false;
  }

  void
  print_status (void)
  {
    if (!initialised)
      {
        log ("[net] not initialised\n");
        return;
      }
    char address[24];
    char mask[24];
    char gateway[24];
    print_ip (address, sizeof (address), *netif_ip4_addr (&et0));
    print_ip (mask, sizeof (mask), *netif_ip4_netmask (&et0));
    print_ip (gateway, sizeof (gateway), *netif_ip4_gw (&et0));
    char b[192];
    std::snprintf (b, sizeof (b),
                   "[net] et0 link %s, address %s mask %s gateway %s\n",
                   netif_is_link_up (&et0) ? "up" : "down", address, mask,
                   gateway);
    log (b);
    std::snprintf (b, sizeof (b),
                   "[net] frames rx %u tx %u dropped %u; echo %u conn, %lu "
                   "bytes\n",
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
        destination = *netif_ip4_gw (&et0);
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

    // Resolve the next hop first.  lwIP drops (or queues only one) frame per
    // unresolved ARP entry, so the first echoes would otherwise be lost while
    // the ARP request/reply round trip is still in flight -- which is exactly
    // how a working link looks like "no reply within 1 s".
    if (IP_IS_V4 (&destination))
      {
        ip4_addr_t next_hop;
        const ip4_addr_t* dst4 = ip_2_ip4 (&destination);
        if (ip4_addr_netcmp (dst4, netif_ip4_addr (&et0),
                             netif_ip4_netmask (&et0)))
          {
            ip4_addr_copy (next_hop, *dst4);
          }
        else
          {
            ip4_addr_copy (next_hop, *netif_ip4_gw (&et0));
          }
        for (unsigned i = 0; i < 100u; ++i)
          {
            bool resolved = false;
            {
              lwip_lock guard;
              if (i == 0)
                {
                  (void)etharp_request (&et0, &next_hop);
                }
              struct eth_addr* eth = nullptr;
              const ip4_addr_t* found = nullptr;
              resolved = etharp_find_addr (&et0, &next_hop, &eth, &found) >= 0;
            }
            if (resolved)
              {
                break;
              }
            os::rtos::sysclock.sleep_for (10);
          }
      }

    raw_pcb* pcb = nullptr;
    {
      lwip_lock guard;
      pcb = raw_new (IP_PROTO_ICMP);
      if (pcb == nullptr)
        {
          return 0;
        }
      raw_recv (pcb, ping_received, nullptr);
      raw_bind (pcb, IP_ADDR_ANY);
    }

    // Each echo is retransmitted (as a real ping tool does): this board's
    // RGMII path drops a noticeable fraction of frames, and one lost echo is
    // not evidence that the network is broken.
    constexpr unsigned attempts_per_echo = 3;
    unsigned replies = 0;
    unsigned retries = 0;
    for (unsigned i = 0; i < count; ++i)
      {
        bool got = false;
        unsigned attempts = 0;
        std::uint32_t rtt_us = 0;
        while (!got && attempts < attempts_per_echo)
          {
            ++attempts;
            pinging.replied = false;
            pinging.seq = static_cast<std::uint16_t> (i);
            pinging.length = 56;

            const std::uint32_t t0 = static_cast<std::uint32_t> (
                os::rtos::sysclock.now ());

            bool sent = false;
            {
              lwip_lock guard;
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
              // lwIP fills the checksum when IP_HLEN is in front of the ICMP
              // header only if we ask; compute it over the ICMP bytes here.
              echo->chksum = 0;
              echo->chksum = inet_chksum (p->payload, 56);

              sent = raw_sendto (pcb, p, &destination) == ERR_OK;
              pbuf_free (p);
            }
            if (!sent)
              {
                break;
              }
            got = wait_for (500, [] { return pinging.replied; });
            if (got)
              {
                rtt_us = (static_cast<std::uint32_t> (
                              os::rtos::sysclock.now ())
                          - t0)
                         * 1000u;
              }
            else if (attempts < attempts_per_echo)
              {
                ++retries;
              }
          }

        if (got)
          {
            ++replies;
            pinging.rtt_us = rtt_us;
            char b[112];
            std::snprintf (b, sizeof (b),
                           "[net] 64 bytes from %u.%u.%u.%u: icmp_seq=%u ttl=%u "
                           "time=%lu.%03lu ms%s\n",
                           ip4_addr1 (&destination), ip4_addr2 (&destination),
                           ip4_addr3 (&destination), ip4_addr4 (&destination),
                           i + 1, static_cast<unsigned> (pinging.ttl),
                           static_cast<unsigned long> (pinging.rtt_us / 1000),
                           static_cast<unsigned long> (pinging.rtt_us % 1000),
                           attempts > 1 ? " (retry)" : "");
            log (b);
          }
        else
          {
            char b[72];
            std::snprintf (b, sizeof (b),
                           "[net] icmp_seq=%u: no reply after %u attempts\n",
                           i + 1, attempts);
            log (b);
          }
        os::rtos::sysclock.sleep_for (200);
      }
    {
      char b[96];
      std::snprintf (b, sizeof (b),
                     "[net] ping: %u/%u replies, %u retransmissions\n",
                     replies, count, retries);
      log (b);
    }
    {
      lwip_lock guard;
      raw_remove (pcb);
    }
    return replies;
  }
} // namespace net_lwip
