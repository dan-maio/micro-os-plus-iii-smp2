// crypto.cpp - compile-time known-answer tests for crypto.hpp, and the one
// test that is too long for the compiler (4096 PBKDF2 rounds) as a run-time
// self-test.
#include "crypto.hpp"

#include <string_view>

namespace mqpro::crypto {

namespace {

constexpr int hex_value(char c) noexcept {
    return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
}

template <std::size_t N>
constexpr std::array<byte, N> hex(std::string_view text) noexcept {
    std::array<byte, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = static_cast<byte>((hex_value(text[i * 2]) << 4) | hex_value(text[i * 2 + 1]));
    }
    return out;
}

template <std::size_t N>
constexpr std::array<byte, N> text_bytes(std::string_view text) noexcept {
    std::array<byte, N> out{};
    for (std::size_t i = 0; i < N; ++i) out[i] = static_cast<byte>(text[i]);
    return out;
}

// FIPS 180: SHA-1("abc")
constexpr bool test_sha1() noexcept {
    sha1 h;
    h.update(text_bytes<3>("abc"));
    return h.finish() == hex<20>("a9993e364706816aba3e25717850c26c9cd0d89d");
}
static_assert(test_sha1());

// RFC 2202, test case 2
constexpr bool test_hmac() noexcept {
    hmac_sha1 h{text_bytes<4>("Jefe")};
    h.update(text_bytes<28>("what do ya want for nothing?"));
    return h.finish() == hex<20>("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
}
static_assert(test_hmac());

// RFC 6070, iteration counts 1 and 2
constexpr bool test_pbkdf2() noexcept {
    std::array<byte, 20> one{};
    std::array<byte, 20> two{};
    pbkdf2_sha1(text_bytes<8>("password"), text_bytes<4>("salt"), 1, one);
    pbkdf2_sha1(text_bytes<8>("password"), text_bytes<4>("salt"), 2, two);
    return one == hex<20>("0c60c80f961f0e71f3a9b524af6012062fe037a6") &&
           two == hex<20>("ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957");
}
static_assert(test_pbkdf2());

// IEEE 802.11 PRF test vector 1 (key = 20 x 0x0b, "prefix", "Hi There")
constexpr bool test_prf() noexcept {
    std::array<byte, 20> key{};
    for (auto& b : key) b = 0x0b;
    std::array<byte, 64> out{};
    prf_sha1(key, text_bytes<6>("prefix"), text_bytes<8>("Hi There"), out);
    return out == hex<64>("bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee"
                          "75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a");
}
static_assert(test_prf());

// FIPS 197, appendix C.1
constexpr bool test_aes() noexcept {
    const aes128 key{hex<16>("000102030405060708090a0b0c0d0e0f")};
    const auto plain = hex<16>("00112233445566778899aabbccddeeff");
    const auto cipher = hex<16>("69c4e0d86a7b0430d8cdb78070b4c55a");
    aes_block out{};
    aes_block back{};
    key.encrypt(plain, out);
    key.decrypt(cipher, back);
    return out == cipher && back == plain;
}
static_assert(test_aes());

// RFC 3394, 4.1: 128-bit key data, 128-bit KEK
constexpr bool test_unwrap() noexcept {
    const aes128 kek{hex<16>("000102030405060708090a0b0c0d0e0f")};
    const auto wrapped = hex<24>("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5");
    std::array<byte, 16> out{};
    return aes_unwrap(kek, wrapped, out) &&
           out == hex<16>("00112233445566778899aabbccddeeff");
}
static_assert(test_unwrap());

// RFC 3610, packet vector #1 (M = 8, L = 2: the CCMP parameters)
constexpr bool test_ccm() noexcept {
    const aes128 key{hex<16>("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf")};
    const ccm_nonce nonce = hex<13>("00000003020100a0a1a2a3a4a5");
    const auto aad = hex<8>("0001020304050607");
    auto data = hex<23>("08090a0b0c0d0e0f101112131415161718191a1b1c1d1e");
    const auto plain = data;
    std::array<byte, 8> mic{};
    ccm_encrypt(key, nonce, aad, data, mic);
    if (data != hex<23>("588c979a61c663d2f066d0c2c0f989806d5f6b61dac384") ||
        mic != hex<8>("17e8d12cfdf926e0")) {
        return false;
    }
    if (!ccm_decrypt(key, nonce, aad, data, mic) || data != plain) {
        return false;
    }
    mic[0] ^= 1;
    data = hex<23>("588c979a61c663d2f066d0c2c0f989806d5f6b61dac384");
    return !ccm_decrypt(key, nonce, aad, data, mic);
}
static_assert(test_ccm());

}  // namespace

// IEEE 802.11 annex J.4.1: passphrase "password", SSID "IEEE".
bool selftest_psk() noexcept {
    std::array<byte, 32> pmk{};
    pbkdf2_sha1(text_bytes<8>("password"), text_bytes<4>("IEEE"), 4096, pmk);
    return pmk == hex<32>("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e");
}

}  // namespace mqpro::crypto
