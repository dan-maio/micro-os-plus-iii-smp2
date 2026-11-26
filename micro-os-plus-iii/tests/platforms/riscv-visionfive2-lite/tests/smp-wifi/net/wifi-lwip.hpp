/*
 * wifi-lwip.hpp -- IPv4 over the Wi-Fi link of the VisionFive 2 Lite (lwIP,
 * raw API, NO_SYS), on µOS++.
 *
 *   netif "wl0"   Ethernet frames <-> wlan::send_ethernet / receive handler
 *   DHCP client   started when the Wi-Fi link comes up
 *   ping          ICMP echo to a dotted address or "gw"
 *   TCP echo      RFC 862 server on port 7
 *
 * ONE THREAD: lwIP (NO_SYS) is not thread-safe and neither is the wlan
 * layer, so everything here except log() belongs to the thread that owns
 * the radio.  log() -- and kprintf(), which ends in it -- may be called from
 * any thread: it is the console's single writer.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wifi_net
{
  /// Create the netif (link down), hook it to the wlan layer, start the TCP
  /// echo server.
  bool
  init (const std::uint8_t* mac);

  /// Run the lwIP timers: DHCP, ARP ageing, TCP retransmissions.
  void
  poll (void);

  /// Hand one received Ethernet frame to lwIP.
  void
  input (const std::uint8_t* frame, std::size_t length);

  /// Tell lwIP that the link came up or went down.
  void
  set_link (bool up);

  [[nodiscard]] bool
  link (void);

  [[nodiscard]] bool
  has_address (void);

  /// Address, mask, gateway and the counters.
  void
  print_status (void);

  /// Wait for a DHCP lease, pumping the radio; false on timeout.
  bool
  wait_for_address (std::uint32_t timeout_ms);

  /// ICMP echo to a dotted address or the literal "gw"; returns the number
  /// of replies.
  unsigned
  ping (std::string_view target, unsigned count);

  inline constexpr std::uint16_t echo_port = 7;

  struct counters
  {
    std::uint32_t rx_frames{ 0 };
    std::uint32_t tx_frames{ 0 };
    std::uint32_t rx_dropped{ 0 };
    std::uint32_t echo_connections{ 0 };
    std::uint64_t echo_bytes{ 0 };
  };

  [[nodiscard]] const counters&
  stats (void);

  /// The single console writer of the test (UART0, one line at a time).
  void
  log (const char* text);

  /// Hardware image: send everything log() printed to the debugger in one
  /// semihosting write, so the runner's log holds the whole run and not only
  /// the verdict (UART0 remains the console).  Nothing elsewhere.
  void
  dump_transcript (void);
} // namespace wifi_net
