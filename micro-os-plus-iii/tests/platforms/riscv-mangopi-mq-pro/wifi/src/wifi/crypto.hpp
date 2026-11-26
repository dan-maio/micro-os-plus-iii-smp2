// crypto.hpp - the cryptography WPA2-PSK needs, and nothing else:
//
//   SHA-1, HMAC-SHA1            PBKDF2 (passphrase -> PMK), PRF (PMK -> PTK),
//                               EAPOL-Key MIC
//   AES-128                     CCMP data protection, EAPOL key-data unwrap
//   AES key unwrap (RFC 3394)   GTK delivery in message 3 of the handshake
//   AES-CCM, M = 8, L = 2       CCMP (IEEE 802.11-2020 12.5.3)
//
// Everything is constexpr, so crypto.cpp checks the published test vectors
// with static_assert: a wrong table or round is a build error, not a
// handshake that silently fails on the board.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mqpro::crypto {

using byte = std::uint8_t;
using bytes = std::span<const byte>;

// ------------------------------------------------------------------- SHA-1
inline constexpr std::size_t sha1_size = 20;
using sha1_digest = std::array<byte, sha1_size>;

class sha1 {
public:
    constexpr sha1() noexcept = default;

    constexpr void update(bytes data) noexcept {
        for (const byte b : data) {
            block_[fill_++] = b;
            if (fill_ == block_.size()) {
                compress();
                fill_ = 0;
            }
        }
        length_ += data.size();
    }

    [[nodiscard]] constexpr sha1_digest finish() noexcept {
        const std::uint64_t bits = length_ * 8u;
        block_[fill_++] = 0x80;
        if (fill_ > 56) {
            while (fill_ < 64) block_[fill_++] = 0;
            compress();
            fill_ = 0;
        }
        while (fill_ < 56) block_[fill_++] = 0;
        for (int i = 7; i >= 0; --i) {
            block_[fill_++] = static_cast<byte>(bits >> (i * 8));
        }
        compress();

        sha1_digest out{};
        for (std::size_t i = 0; i < 5; ++i) {
            out[i * 4 + 0] = static_cast<byte>(state_[i] >> 24);
            out[i * 4 + 1] = static_cast<byte>(state_[i] >> 16);
            out[i * 4 + 2] = static_cast<byte>(state_[i] >> 8);
            out[i * 4 + 3] = static_cast<byte>(state_[i]);
        }
        return out;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t rol(std::uint32_t v, int n) noexcept {
        return (v << n) | (v >> (32 - n));
    }

    constexpr void compress() noexcept {
        std::array<std::uint32_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block_[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block_[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block_[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block_[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3],
                      e = state_[4];
        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdcu;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6u;
            }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<std::uint32_t, 5> state_{0x67452301u, 0xefcdab89u, 0x98badcfeu,
                                        0x10325476u, 0xc3d2e1f0u};
    std::array<byte, 64> block_{};
    std::size_t fill_{0};
    std::uint64_t length_{0};
};

// --------------------------------------------------------------- HMAC-SHA1
class hmac_sha1 {
public:
    constexpr explicit hmac_sha1(bytes key) noexcept {
        std::array<byte, 64> block{};
        if (key.size() > block.size()) {
            sha1 h;
            h.update(key);
            const auto digest = h.finish();
            for (std::size_t i = 0; i < digest.size(); ++i) block[i] = digest[i];
        } else {
            for (std::size_t i = 0; i < key.size(); ++i) block[i] = key[i];
        }
        std::array<byte, 64> ipad{};
        std::array<byte, 64> opad{};
        for (std::size_t i = 0; i < 64; ++i) {
            ipad[i] = block[i] ^ 0x36;
            opad[i] = block[i] ^ 0x5c;
        }
        inner_.update(ipad);
        outer_.update(opad);
    }

    constexpr void update(bytes data) noexcept { inner_.update(data); }

    [[nodiscard]] constexpr sha1_digest finish() noexcept {
        const auto inner = inner_.finish();
        outer_.update(inner);
        return outer_.finish();
    }

private:
    sha1 inner_{};
    sha1 outer_{};
};

/// PBKDF2-HMAC-SHA1 (RFC 8018).  WPA2: PMK = PBKDF2(passphrase, SSID, 4096, 32).
constexpr void pbkdf2_sha1(bytes password, bytes salt, std::uint32_t iterations,
                           std::span<byte> out) noexcept {
    const hmac_sha1 keyed{password};   // pads are hashed once, then copied
    std::uint32_t block_index = 1;
    std::size_t done = 0;
    while (done < out.size()) {
        const std::array<byte, 4> counter{
            static_cast<byte>(block_index >> 24), static_cast<byte>(block_index >> 16),
            static_cast<byte>(block_index >> 8), static_cast<byte>(block_index)};
        hmac_sha1 first = keyed;
        first.update(salt);
        first.update(counter);
        sha1_digest u = first.finish();
        sha1_digest t = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            hmac_sha1 next = keyed;
            next.update(u);
            u = next.finish();
            for (std::size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
        }
        for (std::size_t j = 0; j < t.size() && done < out.size(); ++j) {
            out[done++] = t[j];
        }
        ++block_index;
    }
}

/// IEEE 802.11 PRF (12.7.1.2): HMAC-SHA1(key, label || 0 || data || counter).
constexpr void prf_sha1(bytes key, bytes label, bytes data,
                        std::span<byte> out) noexcept {
    const hmac_sha1 keyed{key};
    std::size_t done = 0;
    byte counter = 0;
    while (done < out.size()) {
        hmac_sha1 h = keyed;
        const std::array<byte, 1> zero{0};
        const std::array<byte, 1> count{counter};
        h.update(label);
        h.update(zero);
        h.update(data);
        h.update(count);
        const auto digest = h.finish();
        for (std::size_t j = 0; j < digest.size() && done < out.size(); ++j) {
            out[done++] = digest[j];
        }
        ++counter;
    }
}

// ----------------------------------------------------------------- AES-128
namespace detail {

[[nodiscard]] constexpr byte xtime(byte v) noexcept {
    return static_cast<byte>((v << 1) ^ ((v & 0x80) != 0 ? 0x1b : 0x00));
}

[[nodiscard]] constexpr byte gmul(byte a, byte b) noexcept {
    byte result = 0;
    while (b != 0) {
        if ((b & 1) != 0) result ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return result;
}

struct aes_tables {
    std::array<byte, 256> sbox{};
    std::array<byte, 256> inv_sbox{};
    std::array<std::uint32_t, 256> te0{};   // {02,01,01,03} * sbox[x], big endian
};

[[nodiscard]] constexpr aes_tables make_aes_tables() noexcept {
    aes_tables t{};
    // Multiplicative inverse through 3^i / 3^(255-i), then the affine map.
    byte p = 1, q = 1;
    do {
        p = static_cast<byte>(p ^ xtime(p));   // p *= 3
        q ^= static_cast<byte>(q << 1);        // q /= 3
        q ^= static_cast<byte>(q << 2);
        q ^= static_cast<byte>(q << 4);
        if ((q & 0x80) != 0) q ^= 0x09;
        const auto rotl8 = [](byte v, int n) {
            return static_cast<byte>((v << n) | (v >> (8 - n)));
        };
        const byte s = static_cast<byte>(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^
                                         rotl8(q, 4) ^ 0x63);
        t.sbox[p] = s;
        t.inv_sbox[s] = p;
    } while (p != 1);
    t.sbox[0] = 0x63;
    t.inv_sbox[0x63] = 0;
    for (std::size_t i = 0; i < 256; ++i) {
        const byte s = t.sbox[i];
        t.te0[i] = (static_cast<std::uint32_t>(xtime(s)) << 24) |
                   (static_cast<std::uint32_t>(s) << 16) |
                   (static_cast<std::uint32_t>(s) << 8) |
                   static_cast<std::uint32_t>(s ^ xtime(s));
    }
    return t;
}

inline constexpr aes_tables aes = make_aes_tables();

[[nodiscard]] constexpr std::uint32_t ror(std::uint32_t v, int n) noexcept {
    return (v >> n) | (v << (32 - n));
}

}  // namespace detail

using aes_block = std::array<byte, 16>;

class aes128 {
public:
    constexpr aes128() noexcept = default;
    constexpr explicit aes128(bytes key) noexcept { set_key(key); }

    constexpr void set_key(bytes key) noexcept {
        for (std::size_t i = 0; i < 4; ++i) {
            rk_[i] = (static_cast<std::uint32_t>(key[i * 4]) << 24) |
                     (static_cast<std::uint32_t>(key[i * 4 + 1]) << 16) |
                     (static_cast<std::uint32_t>(key[i * 4 + 2]) << 8) |
                     static_cast<std::uint32_t>(key[i * 4 + 3]);
        }
        byte rcon = 1;
        for (std::size_t i = 4; i < rk_.size(); ++i) {
            std::uint32_t t = rk_[i - 1];
            if (i % 4 == 0) {
                t = (t << 8) | (t >> 24);
                t = sub_word(t) ^ (static_cast<std::uint32_t>(rcon) << 24);
                rcon = detail::xtime(rcon);
            }
            rk_[i] = rk_[i - 4] ^ t;
        }
    }

    constexpr void encrypt(const aes_block& in, aes_block& out) const noexcept {
        const auto& te0 = detail::aes.te0;
        const auto& sbox = detail::aes.sbox;
        std::uint32_t s0 = load(in, 0) ^ rk_[0];
        std::uint32_t s1 = load(in, 4) ^ rk_[1];
        std::uint32_t s2 = load(in, 8) ^ rk_[2];
        std::uint32_t s3 = load(in, 12) ^ rk_[3];
        for (std::size_t round = 1; round < 10; ++round) {
            const std::uint32_t t0 = te0[s0 >> 24] ^ detail::ror(te0[(s1 >> 16) & 0xff], 8) ^
                                     detail::ror(te0[(s2 >> 8) & 0xff], 16) ^
                                     detail::ror(te0[s3 & 0xff], 24) ^ rk_[round * 4];
            const std::uint32_t t1 = te0[s1 >> 24] ^ detail::ror(te0[(s2 >> 16) & 0xff], 8) ^
                                     detail::ror(te0[(s3 >> 8) & 0xff], 16) ^
                                     detail::ror(te0[s0 & 0xff], 24) ^ rk_[round * 4 + 1];
            const std::uint32_t t2 = te0[s2 >> 24] ^ detail::ror(te0[(s3 >> 16) & 0xff], 8) ^
                                     detail::ror(te0[(s0 >> 8) & 0xff], 16) ^
                                     detail::ror(te0[s1 & 0xff], 24) ^ rk_[round * 4 + 2];
            const std::uint32_t t3 = te0[s3 >> 24] ^ detail::ror(te0[(s0 >> 16) & 0xff], 8) ^
                                     detail::ror(te0[(s1 >> 8) & 0xff], 16) ^
                                     detail::ror(te0[s2 & 0xff], 24) ^ rk_[round * 4 + 3];
            s0 = t0;
            s1 = t1;
            s2 = t2;
            s3 = t3;
        }
        const auto last = [&sbox](std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                  std::uint32_t d) {
            return (static_cast<std::uint32_t>(sbox[a >> 24]) << 24) |
                   (static_cast<std::uint32_t>(sbox[(b >> 16) & 0xff]) << 16) |
                   (static_cast<std::uint32_t>(sbox[(c >> 8) & 0xff]) << 8) |
                   static_cast<std::uint32_t>(sbox[d & 0xff]);
        };
        store(out, 0, last(s0, s1, s2, s3) ^ rk_[40]);
        store(out, 4, last(s1, s2, s3, s0) ^ rk_[41]);
        store(out, 8, last(s2, s3, s0, s1) ^ rk_[42]);
        store(out, 12, last(s3, s0, s1, s2) ^ rk_[43]);
    }

    /// Straightforward inverse cipher; only the key unwrap uses it.
    constexpr void decrypt(const aes_block& in, aes_block& out) const noexcept {
        aes_block s = in;
        add_round_key(s, 10);
        for (std::size_t round = 9;; --round) {
            // InvShiftRows
            aes_block t = s;
            for (std::size_t column = 0; column < 4; ++column) {
                for (std::size_t row = 1; row < 4; ++row) {
                    s[((column + row) % 4) * 4 + row] = t[column * 4 + row];
                }
            }
            for (auto& b : s) b = detail::aes.inv_sbox[b];
            add_round_key(s, round);
            if (round == 0) break;
            // InvMixColumns
            for (std::size_t column = 0; column < 4; ++column) {
                const byte a0 = s[column * 4], a1 = s[column * 4 + 1],
                           a2 = s[column * 4 + 2], a3 = s[column * 4 + 3];
                using detail::gmul;
                s[column * 4] = gmul(a0, 14) ^ gmul(a1, 11) ^ gmul(a2, 13) ^ gmul(a3, 9);
                s[column * 4 + 1] = gmul(a0, 9) ^ gmul(a1, 14) ^ gmul(a2, 11) ^ gmul(a3, 13);
                s[column * 4 + 2] = gmul(a0, 13) ^ gmul(a1, 9) ^ gmul(a2, 14) ^ gmul(a3, 11);
                s[column * 4 + 3] = gmul(a0, 11) ^ gmul(a1, 13) ^ gmul(a2, 9) ^ gmul(a3, 14);
            }
        }
        out = s;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t sub_word(std::uint32_t w) noexcept {
        const auto& sbox = detail::aes.sbox;
        return (static_cast<std::uint32_t>(sbox[w >> 24]) << 24) |
               (static_cast<std::uint32_t>(sbox[(w >> 16) & 0xff]) << 16) |
               (static_cast<std::uint32_t>(sbox[(w >> 8) & 0xff]) << 8) |
               static_cast<std::uint32_t>(sbox[w & 0xff]);
    }
    [[nodiscard]] static constexpr std::uint32_t load(const aes_block& b,
                                                      std::size_t i) noexcept {
        return (static_cast<std::uint32_t>(b[i]) << 24) |
               (static_cast<std::uint32_t>(b[i + 1]) << 16) |
               (static_cast<std::uint32_t>(b[i + 2]) << 8) |
               static_cast<std::uint32_t>(b[i + 3]);
    }
    static constexpr void store(aes_block& b, std::size_t i, std::uint32_t v) noexcept {
        b[i] = static_cast<byte>(v >> 24);
        b[i + 1] = static_cast<byte>(v >> 16);
        b[i + 2] = static_cast<byte>(v >> 8);
        b[i + 3] = static_cast<byte>(v);
    }
    constexpr void add_round_key(aes_block& s, std::size_t round) const noexcept {
        for (std::size_t i = 0; i < 4; ++i) {
            const std::uint32_t k = rk_[round * 4 + i];
            s[i * 4] ^= static_cast<byte>(k >> 24);
            s[i * 4 + 1] ^= static_cast<byte>(k >> 16);
            s[i * 4 + 2] ^= static_cast<byte>(k >> 8);
            s[i * 4 + 3] ^= static_cast<byte>(k);
        }
    }

    std::array<std::uint32_t, 44> rk_{};
};

/// RFC 3394 AES key unwrap.  `wrapped` is n + 1 blocks of 8 bytes, `out`
/// receives n blocks.  Returns false when the integrity check fails.
[[nodiscard]] constexpr bool aes_unwrap(const aes128& kek, bytes wrapped,
                                        std::span<byte> out) noexcept {
    if (wrapped.size() < 16 || wrapped.size() % 8 != 0 ||
        out.size() != wrapped.size() - 8) {
        return false;
    }
    const std::size_t n = out.size() / 8;
    std::array<byte, 8> a{};
    for (std::size_t i = 0; i < 8; ++i) a[i] = wrapped[i];
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = wrapped[i + 8];

    for (int j = 5; j >= 0; --j) {
        for (std::size_t i = n; i >= 1; --i) {
            aes_block in{};
            aes_block plain{};
            const std::uint64_t t = static_cast<std::uint64_t>(n) * static_cast<unsigned>(j) + i;
            for (std::size_t k = 0; k < 8; ++k) {
                in[k] = a[k] ^ static_cast<byte>(t >> ((7 - k) * 8));
                in[k + 8] = out[(i - 1) * 8 + k];
            }
            kek.decrypt(in, plain);
            for (std::size_t k = 0; k < 8; ++k) {
                a[k] = plain[k];
                out[(i - 1) * 8 + k] = plain[k + 8];
            }
        }
    }
    byte diff = 0;
    for (const byte b : a) diff |= static_cast<byte>(b ^ 0xa6);
    return diff == 0;
}

// --------------------------------------------------------------- AES-CCM
// CCM as CCMP uses it: 13-byte nonce (L = 2), 8-byte MIC (M = 8).
inline constexpr std::size_t ccm_nonce_size = 13;
inline constexpr std::size_t ccm_mic_size = 8;
using ccm_nonce = std::array<byte, ccm_nonce_size>;

namespace detail {

constexpr void ccm_auth(const aes128& key, const ccm_nonce& nonce, bytes aad,
                        bytes plain, aes_block& mac) noexcept {
    aes_block b{};
    b[0] = static_cast<byte>((aad.empty() ? 0x00 : 0x40) | (((ccm_mic_size - 2) / 2) << 3) | 1);
    for (std::size_t i = 0; i < ccm_nonce_size; ++i) b[1 + i] = nonce[i];
    b[14] = static_cast<byte>(plain.size() >> 8);
    b[15] = static_cast<byte>(plain.size());
    key.encrypt(b, mac);

    std::size_t fill = 0;
    const auto absorb = [&](byte value) {
        mac[fill++] ^= value;
        if (fill == 16) {
            key.encrypt(mac, mac);
            fill = 0;
        }
    };
    const auto pad = [&] {
        if (fill != 0) {
            key.encrypt(mac, mac);
            fill = 0;
        }
    };
    if (!aad.empty()) {
        absorb(static_cast<byte>(aad.size() >> 8));
        absorb(static_cast<byte>(aad.size()));
        for (const byte v : aad) absorb(v);
        pad();
    }
    for (const byte v : plain) absorb(v);
    pad();
}

constexpr void ccm_ctr(const aes128& key, const ccm_nonce& nonce,
                       std::uint16_t counter, aes_block& stream) noexcept {
    aes_block a{};
    a[0] = 1;   // L - 1
    for (std::size_t i = 0; i < ccm_nonce_size; ++i) a[1 + i] = nonce[i];
    a[14] = static_cast<byte>(counter >> 8);
    a[15] = static_cast<byte>(counter);
    key.encrypt(a, stream);
}

constexpr void ccm_crypt(const aes128& key, const ccm_nonce& nonce,
                         std::span<byte> data) noexcept {
    aes_block stream{};
    std::uint16_t counter = 1;
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (i % 16 == 0) {
            ccm_ctr(key, nonce, counter++, stream);
        }
        data[i] ^= stream[i % 16];
    }
}

}  // namespace detail

/// Encrypt `data` in place and write the 8-byte MIC to `mic`.
constexpr void ccm_encrypt(const aes128& key, const ccm_nonce& nonce, bytes aad,
                           std::span<byte> data, std::span<byte> mic) noexcept {
    aes_block mac{};
    detail::ccm_auth(key, nonce, aad, data, mac);
    aes_block s0{};
    detail::ccm_ctr(key, nonce, 0, s0);
    detail::ccm_crypt(key, nonce, data);
    for (std::size_t i = 0; i < ccm_mic_size; ++i) mic[i] = mac[i] ^ s0[i];
}

/// Decrypt `data` in place; returns false when the MIC does not match.
[[nodiscard]] constexpr bool ccm_decrypt(const aes128& key, const ccm_nonce& nonce,
                                         bytes aad, std::span<byte> data,
                                         bytes mic) noexcept {
    detail::ccm_crypt(key, nonce, data);
    aes_block mac{};
    detail::ccm_auth(key, nonce, aad, data, mac);
    aes_block s0{};
    detail::ccm_ctr(key, nonce, 0, s0);
    byte diff = 0;
    for (std::size_t i = 0; i < ccm_mic_size; ++i) {
        diff |= static_cast<byte>(mic[i] ^ mac[i] ^ s0[i]);
    }
    return diff == 0;
}


// ------------------------------------------------------------------ TKIP
// IEEE 802.11-2020 12.5.4: RC4 per-packet key mixing, Michael MIC and the
// WEP ICV.  Only the group key of a mixed-mode AP is TKIP here; pairwise
// stays CCMP.
inline constexpr std::size_t tkip_header_size = 8;
inline constexpr std::size_t tkip_mic_size = 8;
inline constexpr std::size_t tkip_icv_size = 4;

class arc4 {
public:
    constexpr void set_key(bytes key) noexcept {
        for (std::size_t i = 0; i < s_.size(); ++i) s_[i] = static_cast<byte>(i);
        byte j = 0;
        for (std::size_t i = 0; i < s_.size(); ++i) {
            j = static_cast<byte>(j + s_[i] + key[i % key.size()]);
            const byte t = s_[i];
            s_[i] = s_[j];
            s_[j] = t;
        }
        i_ = 0;
        j_ = 0;
    }

    constexpr byte next() noexcept {
        i_ = static_cast<byte>(i_ + 1);
        j_ = static_cast<byte>(j_ + s_[i_]);
        const byte t = s_[i_];
        s_[i_] = s_[j_];
        s_[j_] = t;
        return s_[static_cast<byte>(s_[i_] + s_[j_])];
    }

    constexpr void crypt(std::span<byte> data) noexcept {
        for (auto& b : data) b ^= next();
    }

private:
    std::array<byte, 256> s_{};
    byte i_{0};
    byte j_{0};
};

/// WEP ICV: CRC-32 (reflected, poly 0xedb88320), final complement.
[[nodiscard]] constexpr std::uint32_t crc32_wep(bytes data) noexcept {
    std::uint32_t crc = 0xffffffffu;
    for (const byte b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

namespace detail {
[[nodiscard]] constexpr std::uint32_t rotl32(std::uint32_t v, int n) noexcept {
    return (v << n) | (v >> (32 - n));
}
[[nodiscard]] constexpr std::uint32_t rotr32(std::uint32_t v, int n) noexcept {
    return (v >> n) | (v << (32 - n));
}
[[nodiscard]] constexpr std::uint16_t rotr16(std::uint16_t v, int n) noexcept {
    return static_cast<std::uint16_t>((v >> n) | (v << (16 - n)));
}
[[nodiscard]] constexpr std::uint32_t xswap32(std::uint32_t v) noexcept {
    return ((v & 0x00ff00ffu) << 8) | ((v & 0xff00ff00u) >> 8);
}
[[nodiscard]] constexpr std::uint32_t le32(const byte* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}
constexpr void put_le32(byte* p, std::uint32_t v) noexcept {
    p[0] = static_cast<byte>(v);
    p[1] = static_cast<byte>(v >> 8);
    p[2] = static_cast<byte>(v >> 16);
    p[3] = static_cast<byte>(v >> 24);
}
constexpr void michael_block(std::uint32_t& l, std::uint32_t& r) noexcept {
    r ^= rotl32(l, 17);
    l += r;
    r ^= xswap32(l);
    l += r;
    r ^= rotl32(l, 3);
    l += r;
    r ^= rotr32(l, 2);
    l += r;
}
}  // namespace detail

/// Michael MIC over pseudo-header (DA, SA, priority, 3 zeros) + MSDU.
constexpr void michael_mic(bytes key, bytes pseudo, bytes payload,
                           std::span<byte, tkip_mic_size> mic) noexcept {
    std::uint32_t l = detail::le32(key.data());
    std::uint32_t r = detail::le32(key.data() + 4);
    for (std::size_t i = 0; i + 4 <= pseudo.size(); i += 4) {
        l ^= detail::le32(pseudo.data() + i);
        detail::michael_block(l, r);
    }
    std::size_t at = 0;
    while (at + 4 <= payload.size()) {
        l ^= detail::le32(payload.data() + at);
        detail::michael_block(l, r);
        at += 4;
    }
    std::array<byte, 4> tail{0x5a, 0, 0, 0};
    for (std::size_t i = 0; at + i < payload.size(); ++i) {
        tail[i] = payload[at + i];
    }
    if (at < payload.size()) {
        tail[payload.size() - at] = 0x5a;
    }
    l ^= detail::le32(tail.data());
    detail::michael_block(l, r);
    detail::michael_block(l, r);
    detail::put_le32(mic.data(), l);
    detail::put_le32(mic.data() + 4, r);
}

/// The 16-bit S-box used by TKIP key mixing.
[[nodiscard]] constexpr std::uint16_t tkip_s(std::uint16_t value) noexcept {
    constexpr std::array<std::uint16_t, 256> sbox{
    0xC6A5, 0xF884, 0xEE99, 0xF68D, 0xFF0D, 0xD6BD, 0xDEB1, 0x9154,
    0x6050, 0x0203, 0xCEA9, 0x567D, 0xE719, 0xB562, 0x4DE6, 0xEC9A,
    0x8F45, 0x1F9D, 0x8940, 0xFA87, 0xEF15, 0xB2EB, 0x8EC9, 0xFB0B,
    0x41EC, 0xB367, 0x5FFD, 0x45EA, 0x23BF, 0x53F7, 0xE496, 0x9B5B,
    0x75C2, 0xE11C, 0x3DAE, 0x4C6A, 0x6C5A, 0x7E41, 0xF502, 0x834F,
    0x685C, 0x51F4, 0xD134, 0xF908, 0xE293, 0xAB73, 0x6253, 0x2A3F,
    0x080C, 0x9552, 0x4665, 0x9D5E, 0x3028, 0x37A1, 0x0A0F, 0x2FB5,
    0x0E09, 0x2436, 0x1B9B, 0xDF3D, 0xCD26, 0x4E69, 0x7FCD, 0xEA9F,
    0x121B, 0x1D9E, 0x5874, 0x342E, 0x362D, 0xDCB2, 0xB4EE, 0x5BFB,
    0xA4F6, 0x764D, 0xB761, 0x7DCE, 0x527B, 0xDD3E, 0x5E71, 0x1397,
    0xA6F5, 0xB968, 0x0000, 0xC12C, 0x4060, 0xE31F, 0x79C8, 0xB6ED,
    0xD4BE, 0x8D46, 0x67D9, 0x724B, 0x94DE, 0x98D4, 0xB0E8, 0x854A,
    0xBB6B, 0xC52A, 0x4FE5, 0xED16, 0x86C5, 0x9AD7, 0x6655, 0x1194,
    0x8ACF, 0xE910, 0x0406, 0xFE81, 0xA0F0, 0x7844, 0x25BA, 0x4BE3,
    0xA2F3, 0x5DFE, 0x80C0, 0x058A, 0x3FAD, 0x21BC, 0x7048, 0xF104,
    0x63DF, 0x77C1, 0xAF75, 0x4263, 0x2030, 0xE51A, 0xFD0E, 0xBF6D,
    0x814C, 0x1814, 0x2635, 0xC32F, 0xBEE1, 0x35A2, 0x88CC, 0x2E39,
    0x9357, 0x55F2, 0xFC82, 0x7A47, 0xC8AC, 0xBAE7, 0x322B, 0xE695,
    0xC0A0, 0x1998, 0x9ED1, 0xA37F, 0x4466, 0x547E, 0x3BAB, 0x0B83,
    0x8CCA, 0xC729, 0x6BD3, 0x283C, 0xA779, 0xBCE2, 0x161D, 0xAD76,
    0xDB3B, 0x6456, 0x744E, 0x141E, 0x92DB, 0x0C0A, 0x486C, 0xB8E4,
    0x9F5D, 0xBD6E, 0x43EF, 0xC4A6, 0x39A8, 0x31A4, 0xD337, 0xF28B,
    0xD532, 0x8B43, 0x6E59, 0xDAB7, 0x018C, 0xB164, 0x9CD2, 0x49E0,
    0xD8B4, 0xACFA, 0xF307, 0xCF25, 0xCAAF, 0xF48E, 0x47E9, 0x1018,
    0x6FD5, 0xF088, 0x4A6F, 0x5C72, 0x3824, 0x57F1, 0x73C7, 0x9751,
    0xCB23, 0xA17C, 0xE89C, 0x3E21, 0x96DD, 0x61DC, 0x0D86, 0x0F85,
    0xE090, 0x7C42, 0x71C4, 0xCCAA, 0x90D8, 0x0605, 0xF701, 0x1C12,
    0xC2A3, 0x6A5F, 0xAEF9, 0x69D0, 0x1791, 0x9958, 0x3A27, 0x27B9,
    0xD938, 0xEB13, 0x2BB3, 0x2233, 0xD2BB, 0xA970, 0x0789, 0x33A7,
    0x2DB6, 0x3C22, 0x1592, 0xC920, 0x8749, 0xAAFF, 0x5078, 0xA57A,
    0x038F, 0x59F8, 0x0980, 0x1A17, 0x65DA, 0xD731, 0x84C6, 0xD0B8,
    0x82C3, 0x29B0, 0x5A77, 0x1E11, 0x7BCB, 0xA8FC, 0x6DD6, 0x2C3A,
    };
    const std::uint16_t high = sbox[(value >> 8) & 0xff];
    return static_cast<std::uint16_t>(sbox[value & 0xff] ^
                                      static_cast<std::uint16_t>((high << 8) | (high >> 8)));
}

[[nodiscard]] constexpr std::uint16_t le16(const byte* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

/// TKIP phase 1: TTAK(TA, TK, TSC[47:16]) -> 80-bit P1K.
constexpr void tkip_phase1(bytes tk, const byte* ta, std::uint32_t iv32,
                           std::array<std::uint16_t, 5>& p1k) noexcept {
    p1k[0] = static_cast<std::uint16_t>(iv32);
    p1k[1] = static_cast<std::uint16_t>(iv32 >> 16);
    p1k[2] = le16(ta + 0);
    p1k[3] = le16(ta + 2);
    p1k[4] = le16(ta + 4);
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::size_t j = 2 * (i & 1);
        p1k[0] = static_cast<std::uint16_t>(p1k[0] + tkip_s(p1k[4] ^ le16(tk.data() + 0 + j)));
        p1k[1] = static_cast<std::uint16_t>(p1k[1] + tkip_s(p1k[0] ^ le16(tk.data() + 4 + j)));
        p1k[2] = static_cast<std::uint16_t>(p1k[2] + tkip_s(p1k[1] ^ le16(tk.data() + 8 + j)));
        p1k[3] = static_cast<std::uint16_t>(p1k[3] + tkip_s(p1k[2] ^ le16(tk.data() + 12 + j)));
        p1k[4] = static_cast<std::uint16_t>(p1k[4] + tkip_s(p1k[3] ^ le16(tk.data() + 0 + j)) +
                                           static_cast<std::uint16_t>(i));
    }
}

/// TKIP phase 2: P1K + TSC[15:0] -> 16-byte WEP seed (the RC4 key).
constexpr void tkip_phase2(bytes tk, const std::array<std::uint16_t, 5>& p1k,
                           std::uint16_t iv16, std::array<byte, 16>& seed) noexcept {
    std::array<std::uint16_t, 6> ppk{};
    for (std::size_t i = 0; i < 5; ++i) ppk[i] = p1k[i];
    ppk[5] = static_cast<std::uint16_t>(p1k[4] + iv16);

    ppk[0] = static_cast<std::uint16_t>(ppk[0] + tkip_s(ppk[5] ^ le16(tk.data() + 0)));
    ppk[1] = static_cast<std::uint16_t>(ppk[1] + tkip_s(ppk[0] ^ le16(tk.data() + 2)));
    ppk[2] = static_cast<std::uint16_t>(ppk[2] + tkip_s(ppk[1] ^ le16(tk.data() + 4)));
    ppk[3] = static_cast<std::uint16_t>(ppk[3] + tkip_s(ppk[2] ^ le16(tk.data() + 6)));
    ppk[4] = static_cast<std::uint16_t>(ppk[4] + tkip_s(ppk[3] ^ le16(tk.data() + 8)));
    ppk[5] = static_cast<std::uint16_t>(ppk[5] + tkip_s(ppk[4] ^ le16(tk.data() + 10)));

    ppk[0] = static_cast<std::uint16_t>(
        ppk[0] + detail::rotr16(static_cast<std::uint16_t>(ppk[5] ^ le16(tk.data() + 12)), 1));
    ppk[1] = static_cast<std::uint16_t>(
        ppk[1] + detail::rotr16(static_cast<std::uint16_t>(ppk[0] ^ le16(tk.data() + 14)), 1));
    ppk[2] = static_cast<std::uint16_t>(ppk[2] + detail::rotr16(ppk[1], 1));
    ppk[3] = static_cast<std::uint16_t>(ppk[3] + detail::rotr16(ppk[2], 1));
    ppk[4] = static_cast<std::uint16_t>(ppk[4] + detail::rotr16(ppk[3], 1));
    ppk[5] = static_cast<std::uint16_t>(ppk[5] + detail::rotr16(ppk[4], 1));

    seed[0] = static_cast<byte>(iv16 >> 8);
    seed[1] = static_cast<byte>(((iv16 >> 8) | 0x20) & 0x7f);
    seed[2] = static_cast<byte>(iv16);
    seed[3] = static_cast<byte>(((ppk[5] ^ le16(tk.data())) >> 1) & 0xff);
    for (std::size_t i = 0; i < 6; ++i) {
        seed[4 + i * 2] = static_cast<byte>(ppk[i]);
        seed[5 + i * 2] = static_cast<byte>(ppk[i] >> 8);
    }
}

}  // namespace mqpro::crypto

namespace mqpro::crypto {
/// Run-time known-answer test of the full WPA2 passphrase-to-PMK derivation.
[[nodiscard]] bool selftest_psk() noexcept;
}  // namespace mqpro::crypto
