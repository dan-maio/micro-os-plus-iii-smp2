// wlan.hpp - IEEE 802.11 station on top of the RTL8723DS driver.
//
// What is implemented is what one infrastructure station needs:
//   * active scan on channels 1..13 (probe request + beacons/probe responses)
//   * open-system authentication and association as an 802.11b/g station
//   * WPA2-PSK: 4-way handshake, group key handshake, CCMP (in software)
//   * Ethernet II <-> 802.11 data frame conversion for the IP stack
//
// Not implemented: WPA/TKIP, WPA3/SAE, protected management frames,
// 802.11n (no HT capabilities are advertised, so the AP never aggregates),
// power save, roaming.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mqpro::wlan {

using mac_address = std::array<std::uint8_t, 6>;

enum class security : std::uint8_t {
    open,
    wpa2_psk,      // RSN, CCMP pairwise + group, PSK: supported
    unsupported,   // WEP, WPA/TKIP, WPA3-only, PMF required, ...
};

struct bss {
    mac_address bssid{};
    std::array<char, 33> ssid{};
    std::uint8_t ssid_length{};
    std::uint8_t channel{};
    std::int8_t rssi{};
    std::uint16_t capability{};
    std::uint16_t beacon_interval{};
    security sec{security::open};
    bool group_tkip{false};       // RSN group cipher is TKIP (mixed mode)
    const char* security_text{""};

    [[nodiscard]] std::string_view name() const noexcept {
        return {ssid.data(), ssid_length};
    }
};

struct counters {
    std::uint32_t rx_frames;
    std::uint32_t rx_data_any;   // data frames addressed to us, before decrypt
    std::uint32_t rx_data;
    std::uint32_t rx_crc_errors;
    std::uint32_t rx_decrypt_errors;
    std::uint32_t rx_replays;
    std::uint32_t tx_data;
    std::uint32_t tx_errors;
    std::uint32_t beacons;
};

/// Bring the chip up (SDIO, power, firmware, MAC/BB/RF).  Idempotent.
bool start() noexcept;
[[nodiscard]] bool started() noexcept;

// Test hook: override the station MAC address while idle.
bool set_own_address(std::span<const std::uint8_t, 6> address) noexcept;
[[nodiscard]] const mac_address& address() noexcept;

/// Active scan; the result table is valid until the next scan.
std::span<const bss> scan() noexcept;

/// Scan for `ssid`, authenticate, associate and (for WPA2) run the 4-way
/// handshake.  An empty passphrase joins an open network.
bool join(std::string_view ssid, std::string_view passphrase) noexcept;
void leave() noexcept;

[[nodiscard]] bool linked() noexcept;
[[nodiscard]] const bss& current() noexcept;
[[nodiscard]] std::int8_t rssi() noexcept;
[[nodiscard]] const counters& stats() noexcept;

/// Receive pending frames and run the link timers.  Call from the main loop.
void poll() noexcept;

/// Send one Ethernet II frame (dst, src, ethertype, payload).
bool send_ethernet(std::span<const std::uint8_t> frame) noexcept;

/// Called for every received data frame, converted to Ethernet II.
using receive_handler = void (*)(std::span<const std::uint8_t> frame);
void set_receive_handler(receive_handler handler) noexcept;

/// Called when the link goes up (after the keys are installed) or down.
using link_handler = void (*)(bool up);
void set_link_handler(link_handler handler) noexcept;

/// 0 = firmware rate adaptation, otherwise a fixed rate in units of 500 kb/s
/// (2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108).
bool set_tx_rate(unsigned rate_500k) noexcept;

}  // namespace mqpro::wlan
