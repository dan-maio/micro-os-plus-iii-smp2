// sdio.hpp - Allwinner D1 SDC1 as an SDIO host for the on-board RTL8723DS.
//
// MQ-Pro wiring (sun20i-d1-mangopi-mq-pro.dts):
//   PG0 = CLK, PG1 = CMD, PG2..PG5 = DAT0..DAT3   (function 2, SDC1)
//   PG17 = WL_REG_ON / reset, active low           (mmc-pwrseq-simple)
//   PG10 = WL host wake (unused here: the driver polls)
//
// The controller is the same "SMC" block as SDC0 (see sd/sd_card.cpp); this
// file adds what an SDIO function needs: CMD5/CMD3/CMD7 enumeration, CMD52
// byte access and CMD53 byte/block transfers through the PIO FIFO.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mqpro::sdio {

struct card_info {
    std::uint32_t ocr{};
    std::uint32_t rca{};
    std::uint32_t clock_hz{};
    std::uint16_t vendor{};   // CIS MANFID, 0x024c = Realtek
    std::uint16_t device{};   // 0xd723 = RTL8723DS
    std::uint8_t functions{};
    std::uint8_t cccr_version{};
};

inline constexpr std::uint32_t block_size = 512;

/// Power-cycle the module, enumerate it, switch to 4-bit / 25 MHz and enable
/// function 1 with 512-byte blocks.
bool init() noexcept;

[[nodiscard]] bool ready() noexcept;
[[nodiscard]] const card_info& info() noexcept;
[[nodiscard]] const char* last_error() noexcept;
[[nodiscard]] std::uint32_t last_status() noexcept;

/// CMD52, one byte.
bool read_byte(std::uint32_t function, std::uint32_t address, std::uint8_t& value) noexcept;
bool write_byte(std::uint32_t function, std::uint32_t address, std::uint8_t value) noexcept;

/// CMD53 on function 1, incrementing address.  `length` is rounded up to a
/// multiple of 4 (byte mode, <= 512) or of 512 (block mode); the buffer must
/// be 4-byte aligned and large enough for the rounded size.
bool read(std::uint32_t address, void* data, std::size_t length) noexcept;
bool write(std::uint32_t address, const void* data, std::size_t length) noexcept;

/// Size actually transferred for a request of `length` bytes.
[[nodiscard]] constexpr std::size_t align_size(std::size_t length) noexcept {
    if (length <= block_size) {
        return (length + 3u) & ~std::size_t{3};
    }
    return (length + block_size - 1u) & ~std::size_t{block_size - 1u};
}

}  // namespace mqpro::sdio
