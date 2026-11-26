// net.hpp - IPv4 networking over the Wi-Fi link (lwIP, raw API, no OS).
//
//   netif "wl0"   Ethernet frames <-> wlan::send_ethernet / receive handler
//   DHCP client   started when the Wi-Fi link comes up
//   ping          ICMP echo to an address, a host name, or "gw"
//   TCP echo      RFC 862 server on port 7 (the second network test)
#pragma once

#include <cstdint>
#include <string_view>

namespace mqpro::net {

/// Initialise lwIP and hook the interface to the wlan layer.  Idempotent.
void init() noexcept;

/// Run lwIP timers (DHCP, ARP, TCP).  Call from the main loop.
void poll() noexcept;

[[nodiscard]] bool has_address() noexcept;

/// Print address, mask, gateway, DNS and the echo-server counters.
void print_status() noexcept;

/// Stop DHCP and use a static address, e.g. ("192.168.1.74/24", "192.168.1.1").
bool add_static_arp(std::string_view ip, std::string_view mac) noexcept;
bool set_static_address(std::string_view address, std::string_view gateway) noexcept;

/// Wait for a DHCP lease; returns false after `timeout_ms`.
bool wait_for_address(std::uint32_t timeout_ms) noexcept;

/// Send `count` ICMP echo requests one second apart and print each reply.
/// `target` is a dotted address, a host name (resolved with DNS) or "gw".
/// Returns the number of replies.
unsigned ping(std::string_view target, unsigned count, unsigned payload_size = 56) noexcept;
bool udp_probe(std::string_view host, unsigned port, unsigned size) noexcept;
void set_dhcp_enabled(bool on) noexcept;

inline constexpr std::uint16_t echo_port = 7;

}  // namespace mqpro::net
