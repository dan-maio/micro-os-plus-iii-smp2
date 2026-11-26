/*
 * net-lwip.h -- lwIP over the VisionFive 2 Lite's GMAC0, on µOS++.
 *
 * One netif ("et0") whose output goes to gmac::send().  The receive side is
 * pull, not push: the receive thread calls net_lwip::input() with a frame it
 * took from the DMA ring, and net_lwip::poll() runs the lwIP timers.  No
 * locks: one thread feeds input(), one thread runs poll(), and the protocol
 * control blocks are only touched from those two.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace net_lwip
{
  /// Create the netif (link down) and the TCP echo server.  The MAC address
  /// is the one the interface reports.
  bool
  init (const std::uint8_t* mac);

  /// Run the lwIP timers: DHCP, ARP ageing, TCP retransmissions.
  void
  poll (void);

  /// Hand one received Ethernet frame to lwIP.
  void
  input (const std::uint8_t* frame, std::size_t length);

  /// Tell lwIP that the carrier came up or went down.
  void
  set_link (bool up);

  [[nodiscard]] bool
  link (void);

  [[nodiscard]] bool
  has_address (void);

  /// Address, mask, gateway, DNS and the counters, one block of lines.
  void
  print_status (void);

  /// Wait for a DHCP lease; false on timeout.
  bool
  wait_for_address (std::uint32_t timeout_ms);

  /// ICMP echo to a dotted address or the literal "gw"; returns the number
  /// of replies.
  unsigned
  ping (std::string_view target, unsigned count);

  inline constexpr std::uint16_t echo_port = 7;

  struct counters
  {
    std::uint32_t rx_frames{0};
    std::uint32_t tx_frames{0};
    std::uint32_t rx_dropped{0};
    std::uint32_t echo_connections{0};
    std::uint64_t echo_bytes{0};
  };

  [[nodiscard]] const counters&
  stats (void);

  /// The single console writer of the test (UART0, one line at a time).
  void
  log (const char* text);

  /// The RAM copy of that log: bytes written, and a copy of a range.  The
  /// USB console thread forwards it to the host, so the same text is
  /// readable as a ttyACM device.
  /// Start a fresh log (DDR keeps the previous run's: the board is not
  /// power-cycled between iterations, and a stale magic would otherwise
  /// make this run append to the last one).
  void
  log_reset (void);

  [[nodiscard]] std::uint32_t
  log_length (void);
  void
  log_copy (std::uint32_t offset, char* out, std::uint32_t length);
} // namespace net_lwip
