// wlan.cpp - 802.11 station: scan, join, WPA2-PSK, data frames.
#include "wlan.hpp"

#include <cstring>

#include "console.hpp"
#include "crypto.hpp"
#include "platform.hpp"
#include "sdio.hpp"

extern "C" {
#include "rtw/rtw.h"
}

namespace mqpro::wlan {

namespace {

using std::uint16_t;
using std::uint32_t;
using std::uint64_t;
using std::uint8_t;

// ------------------------------------------------------------ 802.11 bits
constexpr uint16_t fc_type_mask = 0x000c;
constexpr uint16_t fc_type_mgmt = 0x0000;
constexpr uint16_t fc_type_data = 0x0008;
constexpr uint16_t fc_to_ds = 0x0100;
constexpr uint16_t fc_from_ds = 0x0200;
constexpr uint16_t fc_protected = 0x4000;

constexpr uint8_t st_assoc_resp = 1;
constexpr uint8_t st_probe_req = 4;
constexpr uint8_t st_probe_resp = 5;
constexpr uint8_t st_beacon = 8;
constexpr uint8_t st_disassoc = 10;
constexpr uint8_t st_auth = 11;
constexpr uint8_t st_deauth = 12;

constexpr uint16_t cap_ess = 0x0001;
constexpr uint16_t cap_privacy = 0x0010;
constexpr uint16_t cap_short_preamble = 0x0020;
constexpr uint16_t cap_short_slot = 0x0400;

constexpr std::size_t mgmt_header = 24;
constexpr std::size_t data_header = 24;
constexpr std::size_t ccmp_header = 8;
constexpr std::size_t ccmp_mic = 8;
constexpr std::size_t llc_header = 8;

constexpr std::array<uint8_t, 6> llc_snap{0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
constexpr mac_address broadcast{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

constexpr uint16_t ethertype_eapol = 0x888e;

// 802.11b/g rates in 500 kb/s; the first four are "basic" (bit 7).
constexpr std::array<uint8_t, 8> supported_rates{0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
constexpr std::array<uint8_t, 4> extended_rates{0x30, 0x48, 0x60, 0x6c};

// The RSN element this station sends: pairwise CCMP, PSK; the group cipher is
// the one the AP advertises (CCMP, or TKIP on a mixed-mode AP).
constexpr std::array<uint8_t, 22> make_rsn_element(bool tkip_group) {
    return {0x30, 0x14, 0x01, 0x00,
            0x00, 0x0f, 0xac, static_cast<uint8_t>(tkip_group ? 0x02 : 0x04),
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,   // pairwise: CCMP
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,   // AKM: PSK
            0x00, 0x00};
}
inline constexpr auto rsn_element_ccmp = make_rsn_element(false);
inline constexpr auto rsn_element_tkip = make_rsn_element(true);

// ------------------------------------------------------------------ state
enum class phase : uint8_t {
    idle,
    scanning,
    authenticating,
    associating,
    handshake,
    linked,
};

enum class key_kind : uint8_t { ccmp, tkip };

struct key_slot {
    key_kind kind{key_kind::ccmp};
    crypto::aes128 cipher{};                     // CCMP
    std::array<crypto::byte, 16> tk{};           // TKIP temporal key
    std::array<crypto::byte, 8> tx_mic{};
    std::array<crypto::byte, 8> rx_mic{};
    // The phase-1 key depends on the transmitter address, so TX and RX need
    // their own cache: RX mixes with the AP's address, TX with ours.  A shared
    // cache silently reuses the wrong P1K whenever both directions sit in the
    // same IV32 (which is always the case right after the handshake).
    std::array<uint16_t, 5> p1k{};
    uint32_t p1k_iv32{0};
    bool p1k_valid{false};
    std::array<uint16_t, 5> tx_p1k{};
    uint32_t tx_p1k_iv32{0};
    bool tx_p1k_valid{false};
    uint64_t rx_pn{0};
    bool valid{false};
};

constinit bool chip_started = false;
constinit phase state = phase::idle;
constinit mac_address own_address{};
constinit bss target{};
constinit counters stat{};
constinit receive_handler on_receive = nullptr;
constinit link_handler on_link = nullptr;

constinit std::array<bss, 32> bss_table{};
constinit std::size_t bss_count = 0;

// join progress flags, set by the RX path
constinit bool auth_ok = false;
constinit bool assoc_ok = false;
constinit uint16_t reply_status = 0;
constinit uint16_t association_id = 0;
constinit bool deauthenticated = false;
constinit uint16_t deauth_reason = 0;

// WPA2 state
constinit std::array<uint8_t, 32> pmk{};
constinit std::array<uint8_t, 48> ptk{};   // KCK | KEK | TK
constinit std::array<uint8_t, 32> anonce{};
constinit std::array<uint8_t, 32> snonce{};
constinit uint64_t replay_counter = 0;
constinit bool replay_valid = false;
constinit bool ptk_derived = false;
constinit bool handshake_done = false;
constinit bool handshake_failed = false;
constinit key_slot pairwise_key{};
constinit std::array<key_slot, 4> group_keys{};
constinit uint64_t tx_pn = 0;
constinit uint16_t tx_sequence = 0;
constinit uint64_t tkip_tx_tsc = 0;
constinit uint8_t gtk_id = 1;

constinit bool tx_encrypt = false;

constinit uint32_t last_beacon_ms = 0;
constinit uint32_t last_tx_ms = 0;
constinit std::int8_t last_rssi = -128;
constinit uint8_t tx_fixed_rate = 0xff;   // DESC_RATE*, 0xff = firmware RA
constinit uint32_t nonce_counter = 0;

// One aggregate from the chip: up to the RX FIFO size plus SDIO block padding.
alignas(8) constinit std::array<uint8_t, 16384 + 1024> rx_buffer{};
// TX: descriptor + 802.11 header + CCMP + LLC + payload + MIC, block padded.
alignas(8) constinit std::array<uint8_t, 2560> tx_buffer{};

// --------------------------------------------------------------- helpers
[[nodiscard]] uint32_t now_ms() noexcept { return platform::millis(); }

[[nodiscard]] uint16_t get16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
[[nodiscard]] uint16_t get16be(const uint8_t* p) noexcept {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
void put16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put16be(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

[[nodiscard]] bool same_mac(const uint8_t* a, const uint8_t* b) noexcept {
    return std::memcmp(a, b, 6) == 0;
}

void print_mac(const uint8_t* m) noexcept {
    kprintf("%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

[[nodiscard]] uint8_t rate_to_desc(unsigned rate_500k) noexcept {
    switch (rate_500k) {
    case 2: return DESC_RATE1M;
    case 4: return DESC_RATE2M;
    case 11: return DESC_RATE5_5M;
    case 22: return DESC_RATE11M;
    case 12: return DESC_RATE6M;
    case 18: return DESC_RATE9M;
    case 24: return DESC_RATE12M;
    case 36: return DESC_RATE18M;
    case 48: return DESC_RATE24M;
    case 72: return DESC_RATE36M;
    case 96: return DESC_RATE48M;
    case 108: return DESC_RATE54M;
    default: return 0xff;
    }
}

// ------------------------------------------------------------- transmit
[[nodiscard]] uint8_t* tx_frame() noexcept { return tx_buffer.data() + RTW_TX_DESC_SIZE; }

bool tx_submit(std::size_t length, rtw_tx_queue queue, uint8_t rate, bool fixed,
               bool group) noexcept {
    rtw_tx_info info{};
    info.queue = queue;
    info.rate = rate;
    info.mac_id = 0;
    // The BMC flag follows the 802.11 receiver address (A1), not the Ethernet
    // destination: a station's to-DS broadcast goes to the AP as a unicast
    // frame, so BMC stays 0.
    info.bmc = group;
    info.hw_seq = true;
    info.fixed_rate = fixed;
    const int result = rtw_tx_frame(&rtw, tx_buffer.data(), static_cast<u32>(length), &info);
    if (result != 0) {
        ++stat.tx_errors;
        return false;
    }
    last_tx_ms = now_ms();
    return true;
}

// Management header: FC, duration, DA, SA, BSSID, sequence control.
[[nodiscard]] uint16_t take_sequence() noexcept {
    const uint16_t sequence = tx_sequence;
    tx_sequence = static_cast<uint16_t>((tx_sequence + 1) & 0x0fffu);
    return sequence;
}

std::size_t mgmt_begin(uint8_t subtype, const uint8_t* da, const uint8_t* bssid) noexcept {
    uint8_t* f = tx_frame();
    put16(f, static_cast<uint16_t>(fc_type_mgmt | (subtype << 4)));
    put16(f + 2, 0);                                   // Duration/ID
    put16(f + 22, static_cast<uint16_t>(take_sequence() << 4));   // Sequence Control
    std::memcpy(f + 4, da, 6);
    std::memcpy(f + 10, own_address.data(), 6);
    std::memcpy(f + 16, bssid, 6);
    return mgmt_header;
}

bool mgmt_send(std::size_t length, bool group) noexcept {
    return tx_submit(length, RTW_QUEUE_MGMT, DESC_RATE1M, true, group);
}

std::size_t put_element(std::size_t at, uint8_t id, std::span<const uint8_t> body) noexcept {
    uint8_t* f = tx_frame();
    f[at++] = id;
    f[at++] = static_cast<uint8_t>(body.size());
    std::memcpy(f + at, body.data(), body.size());
    return at + body.size();
}

bool send_probe_request() noexcept {
    std::size_t at = mgmt_begin(st_probe_req, broadcast.data(), broadcast.data());
    at = put_element(at, 0, {});   // wildcard SSID
    at = put_element(at, 1, supported_rates);
    at = put_element(at, 50, extended_rates);
    return mgmt_send(at, true);
}

bool send_authentication() noexcept {
    std::size_t at = mgmt_begin(st_auth, target.bssid.data(), target.bssid.data());
    uint8_t* f = tx_frame();
    put16(f + at, 0);       // open system
    put16(f + at + 2, 1);   // transaction 1
    put16(f + at + 4, 0);   // status
    return mgmt_send(at + 6, false);
}

bool send_association_request() noexcept {
    std::size_t at = mgmt_begin(0, target.bssid.data(), target.bssid.data());
    uint8_t* f = tx_frame();
    uint16_t capability = cap_ess | cap_short_slot;
    if ((target.capability & cap_short_preamble) != 0) {
        capability |= cap_short_preamble;
    }
    if (target.sec != security::open) {
        capability |= cap_privacy;
    }
    put16(f + at, capability);
    put16(f + at + 2, 10);   // listen interval
    at += 4;
    at = put_element(at, 0, {reinterpret_cast<const uint8_t*>(target.ssid.data()),
                             target.ssid_length});
    at = put_element(at, 1, supported_rates);
    at = put_element(at, 50, extended_rates);
    if (target.sec == security::wpa2_psk) {
        const auto& element = target.group_tkip ? rsn_element_tkip : rsn_element_ccmp;
        std::memcpy(f + at, element.data(), element.size());
        at += element.size();
    }
    return mgmt_send(at, false);
}

bool send_deauthentication(uint16_t reason) noexcept {
    std::size_t at = mgmt_begin(st_deauth, target.bssid.data(), target.bssid.data());
    put16(tx_frame() + at, reason);
    return mgmt_send(at + 2, false);
}

// CCMP nonce and AAD for a data frame whose 802.11 header starts at `hdr`.
void ccmp_inputs(const uint8_t* hdr, std::size_t hdr_len, uint64_t pn,
                 crypto::ccm_nonce& nonce, std::array<uint8_t, 30>& aad,
                 std::size_t& aad_len) noexcept {
    const uint16_t fc = get16(hdr);
    const bool qos = (fc & 0x0080) != 0;
    const uint8_t tid = qos ? static_cast<uint8_t>(hdr[hdr_len - 2] & 0x0f) : 0;

    nonce[0] = tid;
    std::memcpy(nonce.data() + 1, hdr + 10, 6);   // A2
    for (int i = 0; i < 6; ++i) {
        nonce[7 + i] = static_cast<uint8_t>(pn >> (8 * (5 - i)));
    }

    // FC with subtype bits 4-6, retry, power management and more-data masked,
    // protected set; sequence control with the sequence number masked.
    uint16_t masked = static_cast<uint16_t>((fc & ~0x3870u) | fc_protected);
    if (qos) {
        masked &= static_cast<uint16_t>(~0x8000u);
    }
    put16(aad.data(), masked);
    std::memcpy(aad.data() + 2, hdr + 4, 18);                // A1 A2 A3
    put16(aad.data() + 20, get16(hdr + 22) & 0x000f);        // fragment number only
    aad_len = 22;
    if (qos) {
        aad[22] = tid;
        aad[23] = 0;
        aad_len = 24;
    }
}

// Data frame to the AP: `payload` follows an LLC/SNAP header with `ethertype`.
bool send_data(const uint8_t* destination, uint16_t ethertype,
               std::span<const uint8_t> payload, bool encrypt, bool low_rate) noexcept {
    if (payload.size() > 1500 + 100) {
        return false;
    }
    uint8_t* f = tx_frame();
    const bool group = (destination[0] & 1) != 0;
    uint16_t fc = fc_type_data | fc_to_ds;
    if (encrypt) {
        fc |= fc_protected;
    }
    put16(f, fc);
    put16(f + 2, 0);                                 // Duration/ID (0 = no NAV)
    put16(f + 22, static_cast<uint16_t>(take_sequence() << 4));   // Sequence Control
    std::memcpy(f + 4, target.bssid.data(), 6);      // A1 = BSSID (receiver)
    std::memcpy(f + 10, own_address.data(), 6);      // A2 = SA
    std::memcpy(f + 16, destination, 6);             // A3 = DA
    std::size_t at = data_header;

    // Key selection follows the 802.11 receiver address, not the Ethernet
    // destination.  Every data frame a station transmits is addressed to the
    // AP (A1 = BSSID), so it is protected with the pairwise key - even when
    // the Ethernet destination is a broadcast (DHCP, ARP, ...).  The AP
    // re-encrypts group traffic with the GTK when it forwards it to the BSS.
    // (This is what rtw88/Realtek and mac80211 do: the group key is only for
    // frames whose A1 is a group address, which never happens on the STA's
    // transmit path.)
    key_slot* key = nullptr;
    if (encrypt) {
        key = &pairwise_key;
        if (!key->valid) {
            return false;
        }
    }
    const std::size_t sec_header =
        (encrypt && key->kind == key_kind::tkip) ? crypto::tkip_header_size
                                                 : (encrypt ? ccmp_header : 0);
    uint8_t* body = f + at + sec_header;
    std::memcpy(body, llc_snap.data(), llc_snap.size());
    put16be(body + 6, ethertype);
    std::memcpy(body + llc_header, payload.data(), payload.size());
    const std::size_t body_len = llc_header + payload.size();

    if (encrypt && key->kind == key_kind::ccmp) {
        const uint64_t pn = ++tx_pn;
        uint8_t* c = f + at;
        c[0] = static_cast<uint8_t>(pn);
        c[1] = static_cast<uint8_t>(pn >> 8);
        c[2] = 0;
        c[3] = 0x20;   // ExtIV, pairwise key id 0
        c[4] = static_cast<uint8_t>(pn >> 16);
        c[5] = static_cast<uint8_t>(pn >> 24);
        c[6] = static_cast<uint8_t>(pn >> 32);
        c[7] = static_cast<uint8_t>(pn >> 40);

        crypto::ccm_nonce nonce{};
        std::array<uint8_t, 30> aad{};
        std::size_t aad_len = 0;
        ccmp_inputs(f, data_header, pn, nonce, aad, aad_len);
        crypto::ccm_encrypt(key->cipher, nonce, {aad.data(), aad_len},
                            {body, body_len}, {body + body_len, ccmp_mic});
        at += ccmp_header + body_len + ccmp_mic;
    } else if (encrypt) {
        // TKIP (only used if the AP picks TKIP as the pairwise cipher).
        const std::uint64_t tsc = ++tkip_tx_tsc;
        const std::uint32_t iv32 = static_cast<std::uint32_t>(tsc >> 16);
        const std::uint16_t iv16 = static_cast<std::uint16_t>(tsc);
        uint8_t* c = f + at;
        c[0] = static_cast<std::uint8_t>(iv16 >> 8);
        c[1] = static_cast<std::uint8_t>(((iv16 >> 8) | 0x20) & 0x7f);
        c[2] = static_cast<std::uint8_t>(iv16);
        c[3] = static_cast<std::uint8_t>((gtk_id << 6) | 0x20);
        c[4] = static_cast<std::uint8_t>(iv32);
        c[5] = static_cast<std::uint8_t>(iv32 >> 8);
        c[6] = static_cast<std::uint8_t>(iv32 >> 16);
        c[7] = static_cast<std::uint8_t>(iv32 >> 24);

        std::array<crypto::byte, 16> pseudo{};
        std::memcpy(pseudo.data(), destination, 6);            // DA
        std::memcpy(pseudo.data() + 6, own_address.data(), 6); // SA
        crypto::michael_mic({key->tx_mic.data(), 8}, {pseudo.data(), pseudo.size()},
                            {body, body_len},
                            std::span<crypto::byte, crypto::tkip_mic_size>{
                                body + body_len, crypto::tkip_mic_size});

        if (!key->tx_p1k_valid || key->tx_p1k_iv32 != iv32) {
            crypto::tkip_phase1({key->tk.data(), 16}, own_address.data(), iv32,
                                key->tx_p1k);
            key->tx_p1k_iv32 = iv32;
            key->tx_p1k_valid = true;
        }
        std::array<crypto::byte, 16> seed{};
        crypto::tkip_phase2({key->tk.data(), 16}, key->tx_p1k, iv16, seed);

        const std::uint32_t crc =
            crypto::crc32_wep({body, body_len + crypto::tkip_mic_size});
        std::array<crypto::byte, 4> icv{static_cast<crypto::byte>(crc),
                                        static_cast<crypto::byte>(crc >> 8),
                                        static_cast<crypto::byte>(crc >> 16),
                                        static_cast<crypto::byte>(crc >> 24)};
        crypto::arc4 rc4;
        rc4.set_key({seed.data(), 16});
        rc4.crypt({body, body_len + crypto::tkip_mic_size});
        for (auto& byte : icv) {
            byte ^= rc4.next();
        }
        std::memcpy(body + body_len + crypto::tkip_mic_size, icv.data(), icv.size());
        at += crypto::tkip_header_size + body_len + crypto::tkip_mic_size +
              crypto::tkip_icv_size;
    } else {
        at += body_len;
    }

    uint8_t rate = DESC_RATE54M;
    bool fixed = false;
    if (low_rate || group) {
        // Broadcast/multicast frames must use a rate from the BSS basic rate
        // set, otherwise no station (not even the AP) can decode them.
        rate = DESC_RATE1M;
        fixed = true;
    } else if (tx_fixed_rate != 0xff) {
        rate = tx_fixed_rate;
        fixed = true;
    }

    // EAPOL is submitted through the management FIFO: the data FIFO path is
    // not exercised by the association frames and has proven unreliable
    // during the 4-way handshake on this chip.
    // TEST: group-addressed frames through the management FIFO (which is
    // known to transmit broadcast frames - probe requests get answered).
    const rtw_tx_queue queue = group ? RTW_QUEUE_MGMT : RTW_QUEUE_DATA;
    if (!tx_submit(at, queue, rate, fixed, group)) {
        return false;
    }
    ++stat.tx_data;
    return true;
}

// Null data frame: keeps the association alive when nothing else is sent.
void send_null() noexcept {
    uint8_t* f = tx_frame();
    put16(f, static_cast<uint16_t>(fc_type_data | (4u << 4) | fc_to_ds));
    put16(f + 2, 0);
    std::memcpy(f + 4, target.bssid.data(), 6);
    std::memcpy(f + 10, own_address.data(), 6);
    std::memcpy(f + 16, target.bssid.data(), 6);
    put16(f + 22, static_cast<uint16_t>(take_sequence() << 4));
    (void)tx_submit(data_header, RTW_QUEUE_MGMT, DESC_RATE1M, true, false);
}

// ----------------------------------------------------- beacon / BSS table
struct rsn_info {
    bool present{false};
    bool group_ccmp{false};
    bool group_tkip{false};
    bool pairwise_ccmp{false};
    bool akm_psk{false};
    bool akm_other{false};
    bool pmf_required{false};
};

[[nodiscard]] rsn_info parse_rsn(const uint8_t* body, std::size_t length) noexcept {
    rsn_info info{};
    info.present = true;
    const auto is_suite = [](const uint8_t* p, uint8_t type) {
        return p[0] == 0x00 && p[1] == 0x0f && p[2] == 0xac && p[3] == type;
    };
    std::size_t at = 2;   // version
    if (length < at + 4) return info;
    info.group_ccmp = is_suite(body + at, 4);
    info.group_tkip = is_suite(body + at, 2);
    at += 4;
    if (length < at + 2) return info;
    std::size_t count = get16(body + at);
    at += 2;
    for (std::size_t i = 0; i < count && length >= at + 4; ++i, at += 4) {
        if (is_suite(body + at, 4)) info.pairwise_ccmp = true;
    }
    if (length < at + 2) return info;
    count = get16(body + at);
    at += 2;
    for (std::size_t i = 0; i < count && length >= at + 4; ++i, at += 4) {
        if (is_suite(body + at, 2)) {
            info.akm_psk = true;
        } else {
            info.akm_other = true;
        }
    }
    if (length >= at + 2) {
        info.pmf_required = (get16(body + at) & 0x0040) != 0;
    }
    return info;
}

void record_bss(const uint8_t* frame, std::size_t length, std::int8_t rssi) noexcept {
    if (length < mgmt_header + 12) {
        return;
    }
    const uint8_t* bssid = frame + 16;
    const uint8_t* fixed = frame + mgmt_header;
    bss entry{};
    std::memcpy(entry.bssid.data(), bssid, 6);
    entry.beacon_interval = get16(fixed + 8);
    entry.capability = get16(fixed + 10);
    entry.channel = rtw.channel;
    entry.rssi = rssi;

    rsn_info rsn{};
    bool wpa1 = false;
    std::size_t at = mgmt_header + 12;
    while (at + 2 <= length) {
        const uint8_t id = frame[at];
        const std::size_t len = frame[at + 1];
        const uint8_t* body = frame + at + 2;
        if (at + 2 + len > length) {
            break;
        }
        if (id == 0 && len <= 32) {
            entry.ssid_length = static_cast<uint8_t>(len);
            std::memcpy(entry.ssid.data(), body, len);
        } else if (id == 3 && len == 1) {
            entry.channel = body[0];
        } else if (id == 48) {
            rsn = parse_rsn(body, len);
        } else if (id == 221 && len >= 4 && body[0] == 0x00 && body[1] == 0x50 &&
                   body[2] == 0xf2 && body[3] == 0x01) {
            wpa1 = true;
        }
        at += 2 + len;
    }

    if ((entry.capability & cap_privacy) == 0) {
        entry.sec = security::open;
        entry.security_text = "open";
    } else if (rsn.present) {
        if (rsn.pmf_required) {
            entry.sec = security::unsupported;
            entry.security_text = "WPA2/3 PMF-required";
        } else if (!rsn.akm_psk) {
            entry.sec = security::unsupported;
            entry.security_text = rsn.akm_other ? "WPA3/802.1X" : "RSN ?";
        } else if (!rsn.pairwise_ccmp) {
            entry.sec = security::unsupported;
            entry.security_text = "WPA2 TKIP-only";
        } else if (!rsn.group_ccmp && !rsn.group_tkip) {
            entry.sec = security::unsupported;
            entry.security_text = "WPA2 (unknown group cipher)";
        } else {
            entry.sec = security::wpa2_psk;
            entry.group_tkip = rsn.group_tkip;
            entry.security_text = rsn.group_ccmp ? "WPA2-PSK" : "WPA2-PSK (TKIP group)";
        }
    } else {
        entry.sec = security::unsupported;
        entry.security_text = wpa1 ? "WPA (TKIP)" : "WEP";
    }

    // A beacon heard on a neighbouring channel reports its own channel in the
    // DS element; keep the strongest observation per BSSID.
    for (std::size_t i = 0; i < bss_count; ++i) {
        if (bss_table[i].bssid == entry.bssid) {
            if (entry.ssid_length == 0) {   // hidden-SSID beacon: keep the name
                entry.ssid = bss_table[i].ssid;
                entry.ssid_length = bss_table[i].ssid_length;
            }
            if (entry.rssi >= bss_table[i].rssi || bss_table[i].ssid_length == 0) {
                bss_table[i] = entry;
            }
            return;
        }
    }
    if (bss_count < bss_table.size()) {
        bss_table[bss_count++] = entry;
    }
}

// ------------------------------------------------------------------ WPA2
constexpr std::size_t eapol_key_fixed = 99;
constexpr uint16_t key_info_version = 0x0007;
constexpr uint16_t key_info_pairwise = 0x0008;
constexpr uint16_t key_info_install = 0x0040;
constexpr uint16_t key_info_ack = 0x0080;
constexpr uint16_t key_info_mic = 0x0100;
constexpr uint16_t key_info_secure = 0x0200;
constexpr uint16_t key_info_encrypted = 0x1000;

void make_snonce() noexcept {
    // No hardware RNG is used here: hash the timer, the addresses and
    // whatever the radio has received so far.  Good enough to make each
    // handshake unique, which is all a nonce has to be.
    crypto::sha1 h;
    const uint64_t t = plat_time_us();
    ++nonce_counter;
    h.update({reinterpret_cast<const uint8_t*>(&t), sizeof(t)});
    h.update({reinterpret_cast<const uint8_t*>(&nonce_counter), sizeof(nonce_counter)});
    h.update(own_address);
    h.update(target.bssid);
    h.update({reinterpret_cast<const uint8_t*>(&stat), sizeof(stat)});
    h.update({rx_buffer.data(), 256});
    const auto first = h.finish();
    crypto::sha1 h2;
    h2.update(first);
    h2.update(anonce);
    const auto second = h2.finish();
    std::memcpy(snonce.data(), first.data(), 20);
    std::memcpy(snonce.data() + 20, second.data(), 12);
}

void derive_ptk() noexcept {
    std::array<uint8_t, 76> data{};
    const uint8_t* aa = target.bssid.data();
    const uint8_t* spa = own_address.data();
    const bool aa_first = std::memcmp(aa, spa, 6) < 0;
    std::memcpy(data.data(), aa_first ? aa : spa, 6);
    std::memcpy(data.data() + 6, aa_first ? spa : aa, 6);
    const bool an_first = std::memcmp(anonce.data(), snonce.data(), 32) < 0;
    std::memcpy(data.data() + 12, an_first ? anonce.data() : snonce.data(), 32);
    std::memcpy(data.data() + 44, an_first ? snonce.data() : anonce.data(), 32);

    constexpr std::string_view label{"Pairwise key expansion"};
    crypto::prf_sha1(pmk, {reinterpret_cast<const uint8_t*>(label.data()), label.size()},
                     data, ptk);
    ptk_derived = true;
    if (rtw_debug != 0) {
        serial.write("[WLAN] dbg anonce=");
        for (const auto byte : anonce) serial.print("{:02x}", static_cast<unsigned>(byte));
        serial.write("\r\n[WLAN] dbg snonce=");
        for (const auto byte : snonce) serial.print("{:02x}", static_cast<unsigned>(byte));
        serial.write("\r\n[WLAN] dbg ptk=");
        for (const auto byte : ptk) serial.print("{:02x}", static_cast<unsigned>(byte));
        serial.write("\r\n");
    }
}

void eapol_mic(const uint8_t* frame, std::size_t length, uint8_t* out) noexcept {
    crypto::hmac_sha1 h{{ptk.data(), 16}};
    h.update({frame, length});
    const auto digest = h.finish();
    std::memcpy(out, digest.data(), 16);
}

bool send_eapol_key(uint16_t key_info, uint8_t version, uint64_t replay,
                    const uint8_t* nonce, std::span<const uint8_t> key_data) noexcept {
    std::array<uint8_t, eapol_key_fixed + 64> frame{};
    const std::size_t length = eapol_key_fixed + key_data.size();
    frame[0] = version;
    frame[1] = 3;   // EAPOL-Key
    put16be(frame.data() + 2, static_cast<uint16_t>(length - 4));
    frame[4] = 2;   // RSN key descriptor
    put16be(frame.data() + 5, key_info);
    put16be(frame.data() + 7, 0);
    for (int i = 0; i < 8; ++i) {
        frame[9 + i] = static_cast<uint8_t>(replay >> (8 * (7 - i)));
    }
    if (nonce != nullptr) {
        std::memcpy(frame.data() + 17, nonce, 32);
    }
    put16be(frame.data() + 97, static_cast<uint16_t>(key_data.size()));
    std::memcpy(frame.data() + eapol_key_fixed, key_data.data(), key_data.size());
    eapol_mic(frame.data(), length, frame.data() + 81);
    if (rtw_debug != 0) {
        serial.print("[WLAN] dbg eapol len={} info=0x{:04x} replay={}\r\n", length,
                     static_cast<unsigned>(key_info), replay);
        serial.write("[WLAN] dbg frame=");
        for (std::size_t i = 0; i < length; ++i) serial.print("{:02x}", static_cast<unsigned>(frame[i]));
        serial.write("\r\n");
    }
    return send_data(target.bssid.data(), ethertype_eapol, {frame.data(), length},
                     tx_encrypt, true);
}

// Find the GTK KDE (dd len 00-0f-ac 01 keyid rsvd key...) in decrypted key data.
bool install_gtk(const uint8_t* data, std::size_t length) noexcept {
    std::size_t at = 0;
    while (at + 2 <= length) {
        const uint8_t id = data[at];
        const std::size_t len = data[at + 1];
        if (id == 0xdd && len == 0) {
            break;   // padding
        }
        if (at + 2 + len > length) {
            break;
        }
        const uint8_t* body = data + at + 2;
        if (id == 0xdd && len >= 6 + 16 && body[0] == 0x00 && body[1] == 0x0f &&
            body[2] == 0xac) {
            // Key Data Encapsulation: type 1 = GTK (the cipher comes from the
            // key length); some APs put the cipher suite type here instead.
            const std::uint8_t type = body[3];
            const std::uint8_t key_id = body[4] & 0x03;
            const std::size_t key_len = len - 6;
            auto& key = group_keys[key_id];
            key = {};
            const bool is_gtk = (type == 0x01);
            if ((is_gtk || type == 0x04) && key_len == 16) {
                key.kind = key_kind::ccmp;
                key.cipher.set_key({body + 6, 16});
            } else if ((is_gtk || type == 0x02) && key_len == 32) {
                key.kind = key_kind::tkip;
                std::memcpy(key.tk.data(), body + 6, 16);
                // The AP's TX MIC key is our RX MIC key and vice versa.
                // wpa_supplicant: for a group key the MIC keys are reversed,
                // rx_mic = key data[16..23], tx_mic = key data[24..31].
                std::memcpy(key.rx_mic.data(), body + 6 + 16, 8);
                std::memcpy(key.tx_mic.data(), body + 6 + 24, 8);
            } else {
                serial.print("[WLAN] GTK KDE type 0x{:02x} of {} bytes: unsupported\r\n",
                             static_cast<unsigned>(type), key_len);
                return false;
            }
            key.rx_pn = 0;
            key.valid = true;
            gtk_id = key_id;
            const char* kind = key.kind == key_kind::tkip ? "TKIP" : "CCMP";
            serial.print("[WLAN] group key {} installed ({})\r\n",
                         static_cast<unsigned>(key_id), kind);
            return true;
        }
        at += 2 + len;
    }
    return false;
}

void handle_eapol(const uint8_t* frame, std::size_t length) noexcept {
    if (target.sec != security::wpa2_psk || length < eapol_key_fixed || frame[1] != 3) {
        return;
    }
    const std::size_t eapol_length = 4u + get16be(frame + 2);
    if (eapol_length > length || eapol_length < eapol_key_fixed || frame[4] != 2) {
        return;
    }
    const uint16_t key_info = get16be(frame + 5);
    const uint8_t version = frame[0];
    if (rtw_debug != 0) {
        serial.print("[WLAN] dbg eapol rx len={} info=0x{:04x}\r\n", length,
                     static_cast<unsigned>(key_info));
    }
    const std::size_t key_data_length = get16be(frame + 97);
    if (eapol_key_fixed + key_data_length > eapol_length) {
        return;
    }
    if ((key_info & key_info_version) != 2) {
        serial.print("[WLAN] EAPOL key descriptor version {} is not supported "
                     "(only HMAC-SHA1/AES)\r\n", key_info & key_info_version);
        handshake_failed = true;
        return;
    }
    uint64_t replay = 0;
    for (int i = 0; i < 8; ++i) {
        replay = (replay << 8) | frame[9 + i];
    }
    if (replay_valid && replay <= replay_counter && (key_info & key_info_mic) != 0) {
        return;   // replayed or retransmitted message 3
    }

    if ((key_info & key_info_mic) == 0) {
        // Message 1 of 4: ANonce.  (Also a retransmission, or a PTK rekey.)
        if ((key_info & (key_info_pairwise | key_info_ack)) !=
            (key_info_pairwise | key_info_ack)) {
            return;
        }
        std::memcpy(anonce.data(), frame + 17, 32);
        make_snonce();
        derive_ptk();
        serial.write("[WLAN] 4-way handshake: 1/4 received, sending 2/4\r\n");
        (void)send_eapol_key(static_cast<uint16_t>(2 | key_info_pairwise | key_info_mic),
                             version, replay, snonce.data(),
                             target.group_tkip ? rsn_element_tkip : rsn_element_ccmp);
        return;
    }

    // Messages with a MIC: verify it over the frame with the MIC field zeroed.
    if (!ptk_derived) {
        return;
    }
    std::array<uint8_t, 512> copy{};
    if (eapol_length > copy.size()) {
        return;
    }
    std::memcpy(copy.data(), frame, eapol_length);
    std::memset(copy.data() + 81, 0, 16);
    std::array<uint8_t, 16> mic{};
    eapol_mic(copy.data(), eapol_length, mic.data());
    if (std::memcmp(mic.data(), frame + 81, 16) != 0) {
        serial.write("[WLAN] EAPOL MIC mismatch - wrong passphrase?\r\n");
        handshake_failed = true;
        return;
    }
    replay_counter = replay;
    replay_valid = true;

    std::array<uint8_t, 256> key_data{};
    std::size_t plain_length = 0;
    if ((key_info & key_info_encrypted) != 0 && key_data_length >= 16) {
        if (key_data_length - 8 > key_data.size()) {
            return;
        }
        const crypto::aes128 kek{{ptk.data() + 16, 16}};
        plain_length = key_data_length - 8;
        if (!crypto::aes_unwrap(kek, {frame + eapol_key_fixed, key_data_length},
                                {key_data.data(), plain_length})) {
            serial.write("[WLAN] EAPOL key data unwrap failed\r\n");
            handshake_failed = true;
            return;
        }
    }

    if ((key_info & key_info_pairwise) != 0) {
        // Message 3 of 4.
        if ((key_info & key_info_install) == 0 ||
            std::memcmp(anonce.data(), frame + 17, 32) != 0) {
            return;
        }
        if (!install_gtk(key_data.data(), plain_length)) {
            serial.write("[WLAN] 4-way handshake: no usable GTK in message 3\r\n");
            handshake_failed = true;
            return;
        }
        serial.write("[WLAN] 4-way handshake: 3/4 verified, sending 4/4\r\n");
        (void)send_eapol_key(static_cast<uint16_t>(2 | key_info_pairwise | key_info_mic |
                                                   key_info_secure),
                             version, replay, nullptr, {});
        // Message 4 goes out in the clear; the keys apply from here on.
        pairwise_key.cipher.set_key({ptk.data() + 32, 16});
        pairwise_key.rx_pn = 0;
        pairwise_key.valid = true;
        tx_pn = 0;
        tx_encrypt = true;
        handshake_done = true;
    } else {
        // Group key handshake, message 1 of 2.
        if (!install_gtk(key_data.data(), plain_length)) {
            return;
        }
        serial.write("[WLAN] group key updated\r\n");
        (void)send_eapol_key(static_cast<uint16_t>(2 | key_info_mic | key_info_secure),
                             version, replay, nullptr, {});
    }
}

// --------------------------------------------------------------- receive
void link_down(const char* why) noexcept {
    const bool was_linked = state == phase::linked;
    state = phase::idle;
    tx_encrypt = false;
    tkip_tx_tsc = 0;
    pairwise_key = {};
    pairwise_key.valid = false;
    for (auto& key : group_keys) {
        key.valid = false;
    }
    if (was_linked) {
        serial.print("[WLAN] link down: {}\r\n", why);
        rtw_set_linked(&rtw, false, 0, 0);
        if (on_link != nullptr) {
            on_link(false);
        }
    }
}

void handle_data(uint8_t* frame, std::size_t length) noexcept {
    const uint16_t fc = get16(frame);
    const uint8_t subtype = static_cast<uint8_t>((fc >> 4) & 0x0f);
    if ((subtype & 0x04) != 0) {
        return;   // null / CF frames carry no data
    }
    if ((fc & (fc_to_ds | fc_from_ds)) != fc_from_ds) {
        return;   // only AP -> station
    }
    if (state < phase::associating || !same_mac(frame + 10, target.bssid.data())) {
        return;
    }
    const bool qos = (subtype & 0x08) != 0;
    std::size_t header = data_header + (qos ? 2u : 0u);
    if (qos && (fc & 0x8000) != 0) {
        header += 4;   // HT control
    }
    if (length < header + llc_header) {
        return;
    }
    const uint8_t* da = frame + 4;
    const uint8_t* sa = frame + 16;
    const bool group = (da[0] & 1) != 0;
    if (!group && !same_mac(da, own_address.data())) {
        return;
    }
    if (same_mac(sa, own_address.data())) {
        return;   // our own frame, relayed back by the AP
    }

    ++stat.rx_data_any;

    uint8_t* body = frame + header;
    std::size_t body_len = length - header;

    if ((fc & fc_protected) != 0) {
        if (body_len < 8 || (body[3] & 0x20) == 0) {
            ++stat.rx_decrypt_errors;
            return;
        }
        key_slot& key = group ? group_keys[body[3] >> 6] : pairwise_key;
        if (!key.valid) {
            ++stat.rx_decrypt_errors;
            if (rtw_debug != 0) {
                serial.print("[WLAN] dbg rx protected, key {} not installed\r\n",
                             static_cast<unsigned>(body[3] >> 6));
            }
            return;
        }
        if (key.kind == key_kind::tkip) {
            if (body_len < crypto::tkip_header_size + crypto::tkip_mic_size +
                               crypto::tkip_icv_size) {
                ++stat.rx_decrypt_errors;
                return;
            }
            const std::uint16_t iv16 = static_cast<std::uint16_t>((body[0] << 8) | body[2]);
            const std::uint32_t iv32 = static_cast<std::uint32_t>(body[4]) |
                                       (static_cast<std::uint32_t>(body[5]) << 8) |
                                       (static_cast<std::uint32_t>(body[6]) << 16) |
                                       (static_cast<std::uint32_t>(body[7]) << 24);
            const std::uint64_t tsc = (static_cast<std::uint64_t>(iv32) << 16) | iv16;
            if (tsc <= key.rx_pn) {
                ++stat.rx_replays;
                return;
            }
            const std::size_t data_len = body_len - crypto::tkip_header_size -
                                         crypto::tkip_mic_size - crypto::tkip_icv_size;
            if (!key.p1k_valid || key.p1k_iv32 != iv32) {
                crypto::tkip_phase1({key.tk.data(), 16}, frame + 10, iv32, key.p1k);
                key.p1k_iv32 = iv32;
                key.p1k_valid = true;
            }
            std::array<crypto::byte, 16> seed{};
            crypto::tkip_phase2({key.tk.data(), 16}, key.p1k, iv16, seed);
            crypto::arc4 rc4;
            rc4.set_key({seed.data(), 16});
            rc4.crypt({body + crypto::tkip_header_size,
                       data_len + crypto::tkip_mic_size + crypto::tkip_icv_size});

            crypto::byte* plain = body + crypto::tkip_header_size;
            const std::uint32_t crc =
                crypto::crc32_wep({plain, data_len + crypto::tkip_mic_size});
            const std::uint32_t sent_icv =
                static_cast<std::uint32_t>(plain[data_len + 8]) |
                (static_cast<std::uint32_t>(plain[data_len + 9]) << 8) |
                (static_cast<std::uint32_t>(plain[data_len + 10]) << 16) |
                (static_cast<std::uint32_t>(plain[data_len + 11]) << 24);
            if (crc != sent_icv) {
                ++stat.rx_crc_errors;
                if (rtw_debug != 0) {
                    serial.print("[WLAN] dbg tkip icv: id={} iv32={:08x} iv16={:04x} "
                                 "len={} crc={:08x} got={:08x} tk=",
                                 static_cast<unsigned>(body[3] >> 6), iv32, iv16,
                                 data_len, crc, sent_icv);
                    for (const auto byte : key.tk) serial.print("{:02x}", static_cast<unsigned>(byte));
                    serial.write("\r\n[WLAN] dbg ct=");
                    for (std::size_t i = 0; i < 24 && i < data_len; ++i) {
                        serial.print("{:02x}", static_cast<unsigned>(plain[i]));
                    }
                    serial.write("\r\n");
                }
                return;
            }
            std::array<crypto::byte, 16> pseudo{};
            std::memcpy(pseudo.data(), frame + 4, 6);      // DA
            std::memcpy(pseudo.data() + 6, frame + 16, 6); // SA
            std::array<crypto::byte, 8> mic{};
            crypto::michael_mic({key.rx_mic.data(), 8}, {pseudo.data(), pseudo.size()},
                                {plain, data_len},
                                std::span<crypto::byte, crypto::tkip_mic_size>{
                                    mic.data(), mic.size()});
            if (std::memcmp(mic.data(), plain + data_len, mic.size()) != 0) {
                ++stat.rx_decrypt_errors;
                return;
            }
            key.rx_pn = tsc;
            body += crypto::tkip_header_size;
            body_len = data_len;
        } else {
            if (body_len < ccmp_header + ccmp_mic) {
                ++stat.rx_decrypt_errors;
                return;
            }
            const uint64_t pn = static_cast<uint64_t>(body[0]) |
                                (static_cast<uint64_t>(body[1]) << 8) |
                                (static_cast<uint64_t>(body[4]) << 16) |
                                (static_cast<uint64_t>(body[5]) << 24) |
                                (static_cast<uint64_t>(body[6]) << 32) |
                                (static_cast<uint64_t>(body[7]) << 40);
            if (pn <= key.rx_pn) {
                ++stat.rx_replays;
                return;
            }
            crypto::ccm_nonce nonce{};
            std::array<uint8_t, 30> aad{};
            std::size_t aad_len = 0;
            ccmp_inputs(frame, data_header + (qos ? 2u : 0u), pn, nonce, aad, aad_len);
            body += ccmp_header;
            body_len -= ccmp_header + ccmp_mic;
            if (!crypto::ccm_decrypt(key.cipher, nonce, {aad.data(), aad_len},
                                     {body, body_len}, {body + body_len, ccmp_mic})) {
                ++stat.rx_decrypt_errors;
                return;
            }
            key.rx_pn = pn;
        }
    } else if (tx_encrypt) {
        // Once keys are installed only EAPOL may arrive unprotected; even
        // that is dropped here, since rekeying happens under the PTK.
        return;
    }

    if (body_len < llc_header || std::memcmp(body, llc_snap.data(), llc_snap.size()) != 0) {
        return;
    }
    const uint16_t ethertype = get16be(body + 6);
    const uint8_t* payload = body + llc_header;
    const std::size_t payload_len = body_len - llc_header;

    if (ethertype == ethertype_eapol) {
        handle_eapol(payload, payload_len);
        return;
    }
    if (state != phase::linked || on_receive == nullptr) {
        return;
    }
    ++stat.rx_data;

    // Rebuild an Ethernet II header in the 14 bytes in front of the payload
    // (the 802.11/LLC headers there are no longer needed).
    mac_address destination{};
    mac_address source{};
    std::memcpy(destination.data(), da, 6);
    std::memcpy(source.data(), sa, 6);
    uint8_t* ethernet = const_cast<uint8_t*>(payload) - 14;
    std::memcpy(ethernet, destination.data(), 6);
    std::memcpy(ethernet + 6, source.data(), 6);
    put16be(ethernet + 12, ethertype);
    on_receive({ethernet, payload_len + 14});
}

void handle_management(const uint8_t* frame, std::size_t length, std::int8_t rssi) noexcept {
    const uint8_t subtype = static_cast<uint8_t>((get16(frame) >> 4) & 0x0f);
    const bool from_target = same_mac(frame + 16, target.bssid.data());
    const bool to_us = same_mac(frame + 4, own_address.data());

    switch (subtype) {
    case st_beacon:
    case st_probe_resp:
        if (state == phase::scanning) {
            record_bss(frame, length, rssi);
        } else if (from_target && subtype == st_beacon) {
            last_beacon_ms = now_ms();
            last_rssi = rssi;
            ++stat.beacons;
        }
        break;
    case st_auth:
        if (state == phase::authenticating && from_target && to_us &&
            length >= mgmt_header + 6 && get16(frame + mgmt_header + 2) == 2) {
            reply_status = get16(frame + mgmt_header + 4);
            auth_ok = true;
        }
        break;
    case st_assoc_resp:
        if (state == phase::associating && from_target && to_us &&
            length >= mgmt_header + 6) {
            reply_status = get16(frame + mgmt_header + 2);
            association_id = get16(frame + mgmt_header + 4) & 0x3fff;
            assoc_ok = true;
        }
        break;
    case st_deauth:
    case st_disassoc:
        if (from_target && (to_us || same_mac(frame + 4, broadcast.data())) &&
            length >= mgmt_header + 2 && state >= phase::authenticating) {
            deauth_reason = get16(frame + mgmt_header);
            deauthenticated = true;
        }
        break;
    default:
        break;
    }
}

// Read and dispatch everything the chip has; returns the number of frames.
unsigned pump() noexcept {
    unsigned frames = 0;
    for (int round = 0; round < 8; ++round) {
        const int got = rtw_rx_poll(&rtw, rx_buffer.data(), static_cast<u32>(rx_buffer.size()));
        if (got <= 0) {
            break;
        }
        uint8_t* cursor = rx_buffer.data();
        u32 remaining = static_cast<u32>(got);
        while (remaining >= RTW_RX_DESC_SIZE) {
            rtw_rx_info info{};
            const u8* payload = nullptr;
            const u32 used = rtw_rx_parse(&rtw, cursor, remaining, &info, &payload);
            if (used == 0) {
                break;
            }
            if (!info.is_c2h) {
                ++frames;
                ++stat.rx_frames;
                if (info.crc_err) {
                    ++stat.rx_crc_errors;
                } else if (info.pkt_len >= 16) {
                    auto* frame = const_cast<uint8_t*>(payload);
                    const uint16_t fc = get16(frame);
                    if ((fc & 0x0003) == 0) {   // protocol version 0
                        if ((fc & fc_type_mask) == fc_type_mgmt && info.pkt_len >= mgmt_header) {
                            handle_management(frame, info.pkt_len, info.rssi_dbm);
                        } else if ((fc & fc_type_mask) == fc_type_data &&
                                   info.pkt_len >= data_header) {
                            if (info.rssi_dbm != -128 && state == phase::linked) {
                                last_rssi = info.rssi_dbm;
                            }
                            handle_data(frame, info.pkt_len);
                        }
                    }
                }
            }
            if (used >= remaining) {
                break;
            }
            cursor += used;
            remaining -= used;
        }
    }
    return frames;
}

// Pump the receiver until `done()` or the timeout; keeps USB alive.
template <typename Predicate>
bool wait_for(uint32_t timeout_ms, Predicate done) noexcept {
    const uint32_t start = now_ms();
    for (;;) {
        (void)pump();
        if (done()) {
            return true;
        }
        if (now_ms() - start >= timeout_ms) {
            return false;
        }
        plat_delay_us(1000);
    }
}

void tune(uint8_t channel) noexcept {
    rtw8723d_set_channel(&rtw, channel);
}

}  // namespace


// Test hook: use a different station address (a fresh identity at the AP).
bool set_own_address(std::span<const std::uint8_t, 6> address) noexcept {
    if (state != phase::idle) {
        return false;
    }
    std::memcpy(own_address.data(), address.data(), 6);
    if (chip_started) {
        rtw_set_mac_addr(&rtw, own_address.data());
    }
    return true;
}

// ------------------------------------------------------------- public API
bool started() noexcept { return chip_started; }
const mac_address& address() noexcept { return own_address; }
bool linked() noexcept { return state == phase::linked; }
const bss& current() noexcept { return target; }
std::int8_t rssi() noexcept { return last_rssi; }
const counters& stats() noexcept { return stat; }
void set_receive_handler(receive_handler handler) noexcept { on_receive = handler; }
void set_link_handler(link_handler handler) noexcept { on_link = handler; }

bool set_tx_rate(unsigned rate_500k) noexcept {
    if (rate_500k == 0) {
        tx_fixed_rate = 0xff;
        return true;
    }
    const uint8_t desc = rate_to_desc(rate_500k);
    if (desc == 0xff) {
        return false;
    }
    tx_fixed_rate = desc;
    return true;
}

bool start() noexcept {
    if (chip_started) {
        return true;
    }
    serial.write("[WLAN] SDIO: power-cycling the module (PG17), enumerating on SDC1 ...\r\n");
    if (!sdio::init()) {
        serial.print("[WLAN] SDIO init FAILED: {} (status 0x{:08x})\r\n",
                     sdio::last_error(), sdio::last_status());
        return false;
    }
    const auto& card = sdio::info();
    serial.print("[WLAN] SDIO card {:04x}:{:04x}, {} function(s), OCR 0x{:06x}, "
                 "RCA 0x{:04x}, {} Hz 4-bit\r\n",
                 card.vendor, card.device, card.functions, card.ocr, card.rca,
                 card.clock_hz);
    if (card.vendor != 0x024c || card.device != 0xd723) {
        serial.write("[WLAN] warning: not the expected RTL8723DS (024c:d723)\r\n");
    }
    if (rtw_core_init() != 0) {
        serial.write("[WLAN] chip bring-up FAILED\r\n");
        return false;
    }
    std::memcpy(own_address.data(), rtw.efuse.addr, 6);
    const bool unset = own_address == mac_address{} || own_address == broadcast ||
                       (own_address[0] & 1) != 0;
    if (unset) {
        // No address programmed: use a locally administered one.
        own_address = {0x02, 0x4d, 0x51, 0x50, 0x52, 0x4f};
        rtw_set_mac_addr(&rtw, own_address.data());
        serial.write("[WLAN] efuse has no MAC address, using 02:4d:51:50:52:4f\r\n");
    }
    state = phase::idle;
    stat = {};
    chip_started = true;
    serial.write("[WLAN] radio ready\r\n");
    return true;
}

std::span<const bss> scan() noexcept {
    bss_count = 0;
    if (!start()) {
        return {};
    }
    const phase previous = state;
    const bool was_linked = previous == phase::linked;
    state = phase::scanning;
    rtw_set_rx_filter(&rtw, false);
    for (uint8_t channel = 1; channel <= 13; ++channel) {
        tune(channel);
        (void)send_probe_request();
        (void)wait_for(60, [] { return false; });
        (void)send_probe_request();
        (void)wait_for(80, [] { return false; });
    }
    state = previous;
    if (was_linked) {
        tune(target.channel);
        rtw_set_rx_filter(&rtw, true);
        last_beacon_ms = now_ms();
    }
    return {bss_table.data(), bss_count};
}

void leave() noexcept {
    if (state >= phase::associating) {
        (void)send_deauthentication(3);   // leaving the BSS
        plat_delay_us(20'000);
    }
    const bool was_linked = state == phase::linked;
    link_down("left by request");
    if (!was_linked && chip_started) {
        rtw_set_linked(&rtw, false, 0, 0);
    }
    state = phase::idle;
}

bool join(std::string_view ssid, std::string_view passphrase) noexcept {
    if (!start()) {
        return false;
    }
    if (state != phase::idle) {
        leave();
    }
    if (ssid.empty() || ssid.size() > 32) {
        serial.write("[WLAN] join: SSID must be 1..32 characters\r\n");
        return false;
    }

    // 1. find the strongest BSS with this SSID
    const bss* best = nullptr;
    for (int attempt = 0; attempt < 3 && best == nullptr; ++attempt) {
        for (const bss& candidate : scan()) {
            if (candidate.name() == ssid && (best == nullptr || candidate.rssi > best->rssi)) {
                best = &candidate;
            }
        }
    }
    if (best == nullptr) {
        serial.print("[WLAN] join: no access point with SSID \"{}\" found\r\n", ssid);
        return false;
    }
    target = *best;
    serial.print("[WLAN] joining \"{}\" on channel {}, {} dBm, {}, BSSID ",
                 target.name(), target.channel, static_cast<int>(target.rssi),
                 target.security_text);
    print_mac(target.bssid.data());
    serial.write("\r\n");

    if (target.sec == security::unsupported) {
        serial.print("[WLAN] join: security \"{}\" is not supported "
                     "(open and WPA2-PSK/CCMP are)\r\n", target.security_text);
        return false;
    }
    if (target.sec == security::wpa2_psk) {
        if (passphrase.size() < 8 || passphrase.size() > 63) {
            serial.write("[WLAN] join: a WPA2 passphrase has 8..63 characters\r\n");
            return false;
        }
        const uint32_t t0 = now_ms();
        crypto::pbkdf2_sha1(
            {reinterpret_cast<const uint8_t*>(passphrase.data()), passphrase.size()},
            {reinterpret_cast<const uint8_t*>(ssid.data()), ssid.size()}, 4096, pmk);
        // The PMK is the network's long-term secret (it joins the network and
        // decrypts captured handshakes) and this console is copied to a RAM
        // log anyone with xfel can read: report the timing only.
        serial.print("[WLAN] PMK derived (PBKDF2-SHA1, 4096 rounds) in {} ms\r\n",
                     now_ms() - t0);
    } else if (!passphrase.empty()) {
        serial.write("[WLAN] note: the network is open, the passphrase is ignored\r\n");
    }

    // 2. tune, calibrate, point the MAC at the BSS
    tune(target.channel);
    rtw8723d_phy_calibration(&rtw);
    rtw_set_bssid(&rtw, target.bssid.data());

    replay_valid = false;
    ptk_derived = false;
    handshake_done = false;
    handshake_failed = false;
    deauthenticated = false;
    tx_encrypt = false;
    tkip_tx_tsc = 0;
    pairwise_key = {};
    pairwise_key.valid = false;
    for (auto& key : group_keys) {
        key.valid = false;
    }

    // 3. open-system authentication
    state = phase::authenticating;
    auth_ok = false;
    for (int attempt = 0; attempt < 4 && !auth_ok; ++attempt) {
        (void)send_authentication();
        (void)wait_for(300, [] { return auth_ok; });
    }
    if (!auth_ok || reply_status != 0) {
        if (auth_ok) {
            serial.print("[WLAN] authentication refused, status {}\r\n", reply_status);
        } else {
            serial.write("[WLAN] authentication: no answer from the AP\r\n");
        }
        state = phase::idle;
        return false;
    }
    serial.write("[WLAN] authenticated (open system)\r\n");

    // 4. association
    state = phase::associating;
    assoc_ok = false;
    for (int attempt = 0; attempt < 4 && !assoc_ok; ++attempt) {
        (void)send_association_request();
        (void)wait_for(300, [] { return assoc_ok; });
    }
    if (!assoc_ok || reply_status != 0) {
        if (assoc_ok) {
            serial.print("[WLAN] association refused, status {}\r\n", reply_status);
        } else {
            serial.write("[WLAN] association: no answer from the AP\r\n");
        }
        state = phase::idle;
        return false;
    }
    serial.print("[WLAN] associated, AID {}\r\n", association_id);

    state = phase::handshake;
    rtw_set_linked(&rtw, true, association_id, target.beacon_interval);
    last_beacon_ms = now_ms();

    // 5. WPA2 4-way handshake (the AP starts it)
    if (target.sec == security::wpa2_psk) {
        const bool finished = wait_for(8000, [] {
            return handshake_done || handshake_failed || deauthenticated;
        });
        if (!finished || !handshake_done) {
            if (deauthenticated) {
                serial.print("[WLAN] deauthenticated during the handshake, reason {} "
                             "(15 = wrong passphrase / handshake timeout)\r\n", deauth_reason);
            } else if (!handshake_failed) {
                serial.write("[WLAN] 4-way handshake timed out\r\n");
            }
            (void)send_deauthentication(3);
            rtw_set_linked(&rtw, false, 0, 0);
            state = phase::idle;
            return false;
        }
        serial.write("[WLAN] WPA2 keys installed (CCMP)\r\n");
    }

    state = phase::linked;
    last_beacon_ms = now_ms();
    last_tx_ms = now_ms();
    serial.print("[WLAN] link up: \"{}\"\r\n", target.name());
    if (on_link != nullptr) {
        on_link(true);
    }
    return true;
}

bool send_ethernet(std::span<const uint8_t> frame) noexcept {
    if (state != phase::linked || frame.size() < 14 || frame.size() > 1514) {
        return false;
    }
    const uint16_t ethertype = get16be(frame.data() + 12);
    return send_data(frame.data(), ethertype, frame.subspan(14), tx_encrypt, false);
}

void poll() noexcept {
    if (!chip_started) {
        return;
    }
    (void)pump();
    if (state != phase::linked) {
        return;
    }
    if (deauthenticated) {
        deauthenticated = false;
        serial.print("[WLAN] deauthenticated by the AP, reason {}\r\n", deauth_reason);
        link_down("deauthenticated");
        return;
    }
    const uint32_t now = now_ms();
    if (now - last_beacon_ms > 10'000) {
        link_down("no beacon for 10 s");
        return;
    }
    if (now - last_tx_ms > 15'000) {
        send_null();
        last_tx_ms = now;
    }
}

}  // namespace mqpro::wlan
