/*
 * wlan.hpp -- an 802.11 station on the VisionFive 2 Lite's AIC8800 module.
 *
 * The same interface as the MangoPi MQ-Pro's wlan layer (test/boards/
 * mangopi-mq-pro/wifi/src/wifi/wlan.hpp), over a very different chip: there
 * the host builds every 802.11 frame and encrypts it (RTL8723DS, SoftMAC);
 * here the firmware scans, associates and encrypts (FullMAC), and the host
 * keeps what a FullMAC leaves to it -- choosing the network, the WPA2 4-way
 * handshake (EAPOL frames travel as ordinary data), and installing the keys
 * the handshake yields.
 *
 * ONE THREAD OWNS THIS LAYER.  Nothing here locks: every function, poll()
 * included, must be called from the thread that called start().
 *
 * NOT YET RUN ON THE BOARD.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace wlan
{
  using mac_address = std::array<std::uint8_t, 6>;

  enum class security : std::uint8_t
  {
    open,
    wpa2_psk,
    unsupported
  };

  struct bss
  {
    mac_address bssid{};
    std::array<char, 33> ssid{};
    std::uint8_t ssid_length{};
    std::uint8_t channel{};
    std::uint16_t frequency{};   // MHz
    std::int8_t rssi{};
    std::uint16_t capability{};
    std::uint16_t beacon_interval{};
    security sec{ security::open };
    bool group_tkip{ false };    // RSN group cipher is TKIP (mixed mode)
    const char* security_text{ "" };

    [[nodiscard]] std::string_view
    name () const noexcept
    {
      return { ssid.data (), ssid_length };
    }
  };

  struct counters
  {
    std::uint32_t rx_data;        // data MPDUs from the chip
    std::uint32_t rx_delivered;   // Ethernet frames handed up
    std::uint32_t rx_dropped;     // malformed / not data / port closed
    std::uint32_t rx_eapol;
    std::uint32_t tx_data;
    std::uint32_t tx_errors;
    std::uint32_t indications;    // firmware messages nobody asked for
  };

  /// SDIO, firmware, MAC configuration, the station interface.  Idempotent.
  bool
  start () noexcept;

  [[nodiscard]] bool
  started () noexcept;

  [[nodiscard]] const mac_address&
  address () noexcept;

  /// Active scan of 2.4 and 5 GHz; the table is valid until the next scan.
  std::span<const bss>
  scan () noexcept;

  /// Scan for `ssid`, associate and (for WPA2) run the 4-way handshake.  An
  /// empty passphrase joins an open network.
  bool
  join (std::string_view ssid, std::string_view passphrase) noexcept;

  void
  leave () noexcept;

  [[nodiscard]] bool
  linked () noexcept;

  [[nodiscard]] const bss&
  current () noexcept;

  [[nodiscard]] const counters&
  stats () noexcept;

  /// Receive pending frames, finish a handshake step, notice a lost link.
  void
  poll () noexcept;

  /// Send one Ethernet II frame (dst, src, ethertype, payload).
  bool
  send_ethernet (std::span<const std::uint8_t> frame) noexcept;

  /// Called for every received data frame, converted to Ethernet II.  Runs
  /// inside poll(); it may call send_ethernet(), nothing else of this layer.
  using receive_handler = void (*) (std::span<const std::uint8_t> frame);
  void
  set_receive_handler (receive_handler handler) noexcept;

  /// Called when the link goes up (keys installed, port open) or down.
  using link_handler = void (*) (bool up);
  void
  set_link_handler (link_handler handler) noexcept;
} // namespace wlan
