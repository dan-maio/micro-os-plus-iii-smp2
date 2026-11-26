// sd_card.hpp - Allwinner D1 SDC0 (microSD) driver.
//
// The D1's SD controller is the Allwinner "SMC" block (not SDHCI): commands
// through CMDR/CARG, data through a 32-bit PIO FIFO.  This driver runs it in
// 4-bit mode at 25 MHz and does single-block transfers; the filesystem layer
// above it does the mapping, so multi-block performance is not needed.
#pragma once

#include <cstdint>

namespace mqpro::sd {

struct card_info {
    std::uint32_t rca{};              // relative card address
    std::uint64_t capacity_blocks{};  // 512-byte blocks
    bool high_capacity{};             // SDHC/SDXC (block addressing)
    std::uint32_t clock_hz{};         // bus clock in use
    std::uint8_t csd[16]{};           // raw CSD
};

class card {
public:
    /// Power up the controller, run the SD identification sequence and switch
    /// to 4-bit / 25 MHz.  Returns false and sets last_error() on failure.
    bool init() noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const card_info& info() const noexcept { return info_; }
    [[nodiscard]] const char* last_error() const noexcept { return error_; }
    [[nodiscard]] std::uint32_t last_command() const noexcept { return last_command_; }
    [[nodiscard]] std::uint32_t last_status() const noexcept { return last_status_; }

    bool read_blocks(std::uint32_t lba, void* dst, std::uint32_t count) noexcept;
    bool write_blocks(std::uint32_t lba, const void* src, std::uint32_t count) noexcept;

    bool read_block(std::uint32_t lba, void* dst) noexcept {
        return read_blocks(lba, dst, 1);
    }
    bool write_block(std::uint32_t lba, const void* src) noexcept {
        return write_blocks(lba, src, 1);
    }

private:
    bool send_command(std::uint32_t index, std::uint32_t argument,
                      std::uint32_t flags, std::uint32_t response[4]) noexcept;
    bool read_single(std::uint32_t lba, void* dst) noexcept;
    bool write_single(std::uint32_t lba, const void* src) noexcept;
    bool set_clock(std::uint32_t target_hz) noexcept;
    void reset_fifo() noexcept;
    void fail(const char* why) noexcept;

    bool ready_{false};
    card_info info_{};
    const char* error_{"not initialised"};
    std::uint32_t last_command_{0xffffu};
    std::uint32_t last_status_{0};
};

/// The one card on the board (SDC0).
extern card sdcard;

}  // namespace mqpro::sd
