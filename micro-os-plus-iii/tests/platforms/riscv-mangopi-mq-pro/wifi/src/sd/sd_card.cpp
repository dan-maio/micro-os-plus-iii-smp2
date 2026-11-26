// sd_card.cpp - Allwinner D1 SDC0 driver.
//
// Register map from the D1/H6 "SMC" controller (see the D1 user manual, SDC
// chapter; the same block Linux drives as sunxi-mmc).  Transfers are PIO
// through the 32-bit FIFO at 0x200; no DMA, no interrupts, no high-speed
// timing: 4-bit, 25 MHz, single block.
#include "sd_card.hpp"

#include <cstddef>
#include <cstdint>

#include "d1_board.hpp"
#include "gpio.hpp"
#include "mmio.hpp"

namespace mqpro::sd {

namespace {

constexpr std::uintptr_t sdc_base = 0x0402'0000u;   // SDC0
constexpr std::uintptr_t ccu_base = 0x0200'1000u;

// CCU: bus gate/reset for MMC0, its clock, and PLL_PERIPH0.
constexpr std::uint32_t ccu_bus_mmc0 = 0x084cu;  // bit0 gate, bit16 reset
constexpr std::uint32_t ccu_mmc0_clk = 0x0830u;  // mux/M/P/gate + post-div 2
constexpr std::uint32_t ccu_periph0 = 0x0020u;

// SDC registers
constexpr std::uint32_t r_gctrl = 0x000;
constexpr std::uint32_t r_clkcr = 0x004;
constexpr std::uint32_t r_tmout = 0x008;
constexpr std::uint32_t r_width = 0x00c;
constexpr std::uint32_t r_blksz = 0x010;
constexpr std::uint32_t r_bcntr = 0x014;
constexpr std::uint32_t r_cmdr = 0x018;
constexpr std::uint32_t r_carg = 0x01c;
constexpr std::uint32_t r_resp0 = 0x020;
constexpr std::uint32_t r_resp1 = 0x024;
constexpr std::uint32_t r_resp2 = 0x028;
constexpr std::uint32_t r_resp3 = 0x02c;
constexpr std::uint32_t r_imask = 0x030;
constexpr std::uint32_t r_rintr = 0x038;
constexpr std::uint32_t r_stas = 0x03c;
constexpr std::uint32_t r_ftrgl = 0x040;
constexpr std::uint32_t r_cbcr = 0x048;
constexpr std::uint32_t r_fifo = 0x200;

// GCTRL
constexpr std::uint32_t gc_soft_reset = 1u << 0;
constexpr std::uint32_t gc_fifo_reset = 1u << 1;
constexpr std::uint32_t gc_dma_reset = 1u << 2;
constexpr std::uint32_t gc_int_enable = 1u << 4;
constexpr std::uint32_t gc_dma_enable = 1u << 5;
constexpr std::uint32_t gc_access_by_ahb = 1u << 31;

// CLKCR
constexpr std::uint32_t clk_card_on = 1u << 16;

// WIDTH
constexpr std::uint32_t width_4bit = 1u;

// CMDR
constexpr std::uint32_t cmd_resp_expire = 1u << 6;
constexpr std::uint32_t cmd_long_response = 1u << 7;
constexpr std::uint32_t cmd_check_crc = 1u << 8;
constexpr std::uint32_t cmd_data_expire = 1u << 9;
constexpr std::uint32_t cmd_write = 1u << 10;
constexpr std::uint32_t cmd_init_sequence = 1u << 15;
constexpr std::uint32_t cmd_wait_pre_over = 1u << 13;
constexpr std::uint32_t cmd_upclk_only = 1u << 21;
constexpr std::uint32_t cmd_start = 1u << 31;

// RINTR
constexpr std::uint32_t ir_resp_error = 1u << 1;
constexpr std::uint32_t ir_cmd_done = 1u << 2;
constexpr std::uint32_t ir_data_over = 1u << 3;
constexpr std::uint32_t ir_tx_req = 1u << 4;
constexpr std::uint32_t ir_rx_req = 1u << 5;
constexpr std::uint32_t ir_resp_crc = 1u << 6;
constexpr std::uint32_t ir_data_crc = 1u << 7;
constexpr std::uint32_t ir_resp_timeout = 1u << 8;
constexpr std::uint32_t ir_data_timeout = 1u << 9;
constexpr std::uint32_t ir_fifo_run = 1u << 11;
constexpr std::uint32_t ir_hw_locked = 1u << 12;
constexpr std::uint32_t ir_start_bit = 1u << 13;
constexpr std::uint32_t ir_auto_cmd = 1u << 14;
constexpr std::uint32_t ir_end_bit = 1u << 15;

constexpr std::uint32_t ir_error = ir_resp_error | ir_resp_crc | ir_data_crc |
                                   ir_resp_timeout | ir_data_timeout |
                                   ir_fifo_run | ir_hw_locked | ir_start_bit |
                                   ir_end_bit;

// STAS
constexpr std::uint32_t st_fifo_empty = 1u << 2;
constexpr std::uint32_t st_fifo_full = 1u << 3;
constexpr std::uint32_t st_card_busy = 1u << 9;

constexpr std::uint32_t block_size = 512;
constexpr std::uint32_t block_words = block_size / 4;

// GPIO port F (PF0..PF5 = SDC0), 0x30 bytes per port.
constexpr std::uint32_t port_f = 0x00f0u;

inline std::uint32_t rd(std::uint32_t offset) noexcept {
    return mmio::reg32{sdc_base + offset}.read();
}

inline void wr(std::uint32_t offset, std::uint32_t value) noexcept {
    mmio::reg32{sdc_base + offset}.write(value);
}

inline std::uint64_t ticks() noexcept {
    std::uint64_t value;
    __asm__ volatile("rdtime %0" : "=r"(value));
    return value;
}

inline void delay_us(std::uint32_t us) noexcept {
    const std::uint64_t start = ticks();
    const std::uint64_t want = static_cast<std::uint64_t>(us) * (board::timer_hz / 1'000'000u);
    while (ticks() - start < want) {
    }
}

void configure_pins() noexcept {
    // PF0..PF5 = SDC0 (function 2).  Pull-ups on CMD and DAT0..DAT3.
    for (std::uint32_t i = 0; i < 6; ++i) {
        const board::pin pin{port_f, i};
        gpio::set_function(pin, static_cast<board::mux>(2));
        gpio::set_pull(pin, i == 2 ? board::pull::none : board::pull::up);
    }
}

}  // namespace

card sdcard;

void card::fail(const char* why) noexcept { error_ = why; }

namespace {

[[nodiscard]] const char* describe(std::uint32_t status) noexcept {
    if ((status & ir_resp_timeout) != 0) return "response timeout";
    if ((status & ir_resp_crc) != 0) return "response crc";
    if ((status & ir_resp_error) != 0) return "response error";
    if ((status & ir_data_timeout) != 0) return "data timeout";
    if ((status & ir_data_crc) != 0) return "data crc";
    if ((status & ir_start_bit) != 0) return "start bit error";
    if ((status & ir_end_bit) != 0) return "end bit error";
    if ((status & ir_fifo_run) != 0) return "fifo run error";
    if ((status & ir_hw_locked) != 0) return "hardware locked";
    return "command error";
}

}  // namespace

void card::reset_fifo() noexcept {
    wr(r_gctrl, gc_fifo_reset);
    for (int i = 0; i < 1000 && (rd(r_gctrl) & gc_fifo_reset) != 0; ++i) {
    }
}

bool card::send_command(std::uint32_t index, std::uint32_t argument,
                        std::uint32_t flags, std::uint32_t response[4]) noexcept {
    last_command_ = index;
    last_status_ = 0;
    wr(r_rintr, 0xffffffffu);
    wr(r_carg, argument);
    wr(r_cmdr, flags | (index & 0x3fu) | cmd_start);

    const std::uint64_t deadline = ticks() + 240'000ull;  // 10 ms
    for (;;) {
        const std::uint32_t status = rd(r_rintr);
        if ((status & ir_error) != 0) {
            last_status_ = status;
            wr(r_rintr, status);
            fail(describe(status));
            return false;
        }
        if ((status & ir_cmd_done) != 0) {
            break;
        }
        if (ticks() > deadline) {
            fail("command timeout");
            return false;
        }
    }
    // Read the response before acknowledging the interrupt: clearing
    // COMMAND_DONE first can invalidate a 128-bit (R2) response.
    if (response != nullptr) {
        response[0] = rd(r_resp0);
        response[1] = rd(r_resp1);
        response[2] = rd(r_resp2);
        response[3] = rd(r_resp3);
    }
    wr(r_rintr, ir_cmd_done);
    return true;
}

bool card::set_clock(std::uint32_t target_hz) noexcept {
    // The D1 SDMMC module clock is sourced from the 24 MHz HOSC (mux 0); the
    // SPL's mmc_set_mclk() uses the same parent for its 400 kHz init clock.
    // The CCU divides it by m * 2^n, the controller divides again by 1..256.
    wr(r_clkcr, rd(r_clkcr) & ~clk_card_on);

    constexpr std::uint32_t parent_hz = 24'000'000;
    const std::uint32_t wanted =
        target_hz * 2u > 50'000'000u ? 50'000'000u : target_hz * 2u;

    std::uint32_t best_m = 1;
    std::uint32_t best_n = 0;
    std::uint32_t best_rate = 0;
    for (std::uint32_t n = 0; n <= 3; ++n) {
        for (std::uint32_t m = 1; m <= 16; ++m) {
            const std::uint32_t rate = parent_hz / (m << n);
            if (rate == 0 || rate > wanted) {
                continue;
            }
            if (rate > best_rate) {
                best_rate = rate;
                best_m = m;
                best_n = n;
            }
        }
    }
    if (best_rate == 0) {
        fail("no usable module clock");
        return false;
    }

    mmio::reg32{ccu_base + ccu_mmc0_clk}.write(((best_m - 1u) & 0xfu) |
                                               (best_n << 8) |
                                               (1u << 31));

    std::uint32_t divider = (best_rate + target_hz / 2u) / target_hz;
    if (divider == 0) {
        divider = 1;
    }
    if (divider > 256) {
        divider = 256;
    }

    std::uint32_t clkcr = rd(r_clkcr) & ~0xffu;
    clkcr |= (divider - 1u) & 0xffu;
    wr(r_clkcr, clkcr | clk_card_on);

    // Let the controller latch the new clock.
    wr(r_cmdr, cmd_start | cmd_upclk_only | cmd_wait_pre_over);
    for (int i = 0; i < 100'000 && (rd(r_cmdr) & cmd_start) != 0; ++i) {
    }
    wr(r_rintr, 0xffffffffu);

    info_.clock_hz = best_rate / divider;
    return true;
}

bool card::init() noexcept {
    ready_ = false;

    // CCU: clock the bus, release the reset.
    mmio::reg32{ccu_base + ccu_bus_mmc0}.set(1u << 0);
    mmio::reg32{ccu_base + ccu_bus_mmc0}.clear(1u << 16);
    delay_us(1000);
    mmio::reg32{ccu_base + ccu_bus_mmc0}.set(1u << 16);
    delay_us(1000);

    configure_pins();

    // Controller reset.
    wr(r_gctrl, gc_soft_reset | gc_fifo_reset | gc_dma_reset);
    for (int i = 0; i < 100'000 && (rd(r_gctrl) & (gc_soft_reset | gc_fifo_reset | gc_dma_reset)) != 0; ++i) {
    }
    wr(r_gctrl, gc_int_enable);
    wr(r_imask, 0);
    wr(r_tmout, 0xffffffffu);
    wr(r_ftrgl, 16);
    wr(r_cbcr, 0);
    wr(r_width, 0);  // 1-bit until the card is identified

    if (!set_clock(400'000)) {
        return false;
    }

    std::uint32_t resp[4]{};

    // CMD0: go idle.
    if (!send_command(0, 0, cmd_init_sequence, nullptr)) {
        return false;
    }
    delay_us(2000);

    // CMD8: voltage check / SD v2.
    const bool v2 = send_command(8, 0x1aau, cmd_resp_expire | cmd_check_crc, resp) &&
                    ((resp[0] & 0xfffu) == 0x1aau);

    // ACMD41 until the card leaves idle.
    bool ocr_ready = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (!send_command(55, 0, cmd_resp_expire | cmd_check_crc, resp)) {
            return false;
        }
        // ACMD41 answers with R3, which has no CRC7: do not check one.
        if (!send_command(41, 0x40ff8000u, cmd_resp_expire, resp)) {
            return false;
        }
        if ((resp[0] & 0x8000'0000u) != 0) {
            ocr_ready = true;
            break;
        }
        delay_us(1000);
    }
    if (!ocr_ready) {
        fail("card never became ready");
        return false;
    }
    info_.high_capacity = (resp[0] & 0x4000'0000u) != 0;
    (void)v2;

    // CMD2: CID.
    if (!send_command(2, 0, cmd_resp_expire | cmd_long_response | cmd_check_crc, resp)) {
        return false;
    }

    // CMD3: relative card address.
    if (!send_command(3, 0, cmd_resp_expire | cmd_check_crc, resp)) {
        return false;
    }
    info_.rca = (resp[0] >> 16) & 0xffffu;

    // CMD9: CSD.
    if (!send_command(9, info_.rca << 16, cmd_resp_expire | cmd_long_response | cmd_check_crc, resp)) {
        return false;
    }
    // The controller stores a 128-bit response with RESP3 holding the first
    // word (the SPL's mmc_bsp.c does the same: response[0] = resp3).
    const std::uint32_t csd_words[4] = {resp[3], resp[2], resp[1], resp[0]};
    for (int i = 0; i < 4; ++i) {
        info_.csd[i * 4 + 0] = static_cast<std::uint8_t>(csd_words[i] >> 24);
        info_.csd[i * 4 + 1] = static_cast<std::uint8_t>(csd_words[i] >> 16);
        info_.csd[i * 4 + 2] = static_cast<std::uint8_t>(csd_words[i] >> 8);
        info_.csd[i * 4 + 3] = static_cast<std::uint8_t>(csd_words[i]);
    }

    const std::uint32_t csd_structure = info_.csd[0] >> 6;
    if (csd_structure == 1) {
        const std::uint32_t c_size = ((info_.csd[7] & 0x3fu) << 16) |
                                     (static_cast<std::uint32_t>(info_.csd[8]) << 8) |
                                     info_.csd[9];
        info_.capacity_blocks = (static_cast<std::uint64_t>(c_size) + 1u) * 1024u;
    } else {
        const std::uint32_t c_size = ((info_.csd[6] & 0x03u) << 10) |
                                     (static_cast<std::uint32_t>(info_.csd[7]) << 2) |
                                     (info_.csd[8] >> 6);
        const std::uint32_t c_size_mult = ((info_.csd[9] & 0x03u) << 1) |
                                          (info_.csd[10] >> 7);
        const std::uint32_t read_bl_len = info_.csd[5] & 0x0fu;
        info_.capacity_blocks =
            (static_cast<std::uint64_t>(c_size) + 1u) *
            (1ull << (c_size_mult + 2u)) *
            (1ull << read_bl_len) / 512u;
    }

    // CMD7: select the card.
    if (!send_command(7, info_.rca << 16, cmd_resp_expire | cmd_check_crc, resp)) {
        return false;
    }

    // ACMD6: 4-bit bus.
    if (!send_command(55, info_.rca << 16, cmd_resp_expire | cmd_check_crc, resp)) {
        return false;
    }
    if (!send_command(6, 2, cmd_resp_expire | cmd_check_crc, resp)) {
        return false;
    }
    wr(r_width, width_4bit);

    if (!set_clock(25'000'000)) {
        return false;
    }

    ready_ = true;
    error_ = "";
    return true;
}

bool card::read_single(std::uint32_t lba, void* dst) noexcept {
    reset_fifo();
    wr(r_blksz, block_size);
    wr(r_bcntr, block_size);
    // Access the data port through the AHB (PIO); the controller is in DMA
    // mode after reset, which leaves the FIFO unusable from the CPU.
    wr(r_gctrl, rd(r_gctrl) | gc_access_by_ahb);

    const std::uint32_t address = info_.high_capacity ? lba : lba * block_size;
    if (!send_command(17, address,
                      cmd_resp_expire | cmd_check_crc | cmd_data_expire |
                          cmd_wait_pre_over,
                      nullptr)) {
        return false;
    }

    auto* out = static_cast<std::uint32_t*>(dst);
    for (std::uint32_t i = 0; i < block_words; ++i) {
        const std::uint64_t word_deadline = ticks() + 24'000ull;  // 1 ms
        while ((rd(r_stas) & st_fifo_empty) != 0) {
            if (ticks() > word_deadline) {
                fail("read fifo timeout");
                return false;
            }
        }
        out[i] = rd(r_fifo);
    }

    const std::uint64_t deadline = ticks() + 24'000ull * 500ull;
    for (;;) {
        const std::uint32_t status = rd(r_rintr);
        if ((status & ir_error) != 0) {
            last_status_ = status;
            wr(r_rintr, status);
            fail(describe(status));
            return false;
        }
        if ((status & ir_data_over) != 0) {
            break;
        }
        if (ticks() > deadline) {
            fail("read timeout");
            return false;
        }
    }
    wr(r_rintr, ir_data_over | ir_rx_req);
    return true;
}

bool card::write_single(std::uint32_t lba, const void* src) noexcept {
    reset_fifo();
    wr(r_blksz, block_size);
    wr(r_bcntr, block_size);
    wr(r_gctrl, rd(r_gctrl) | gc_access_by_ahb);

    const std::uint32_t address = info_.high_capacity ? lba : lba * block_size;
    if (!send_command(24, address,
                      cmd_resp_expire | cmd_check_crc | cmd_data_expire |
                          cmd_write | cmd_wait_pre_over,
                      nullptr)) {
        return false;
    }

    const auto* in = static_cast<const std::uint32_t*>(src);
    for (std::uint32_t i = 0; i < block_words; ++i) {
        const std::uint64_t word_deadline = ticks() + 24'000ull;  // 1 ms
        while ((rd(r_stas) & st_fifo_full) != 0) {
            if (ticks() > word_deadline) {
                fail("write fifo timeout");
                return false;
            }
        }
        wr(r_fifo, in[i]);
    }

    const std::uint64_t deadline = ticks() + 24'000ull * 500ull;
    for (;;) {
        const std::uint32_t status = rd(r_rintr);
        if ((status & ir_error) != 0) {
            last_status_ = status;
            wr(r_rintr, status);
            fail(describe(status));
            return false;
        }
        if ((status & ir_data_over) != 0) {
            break;
        }
        if (ticks() > deadline) {
            fail("write timeout");
            return false;
        }
    }
    wr(r_rintr, ir_data_over | ir_tx_req);

    const std::uint64_t busy_deadline = ticks() + 24'000ull * 500ull;
    while ((rd(r_stas) & st_card_busy) != 0) {
        if (ticks() > busy_deadline) {
            fail("card busy");
            return false;
        }
    }
    return true;
}

bool card::read_blocks(std::uint32_t lba, void* dst, std::uint32_t count) noexcept {
    if (!ready_) {
        fail("card not ready");
        return false;
    }
    auto* out = static_cast<std::uint8_t*>(dst);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!read_single(lba + i, out + static_cast<std::size_t>(i) * block_size)) {
            return false;
        }
    }
    return true;
}

bool card::write_blocks(std::uint32_t lba, const void* src, std::uint32_t count) noexcept {
    if (!ready_) {
        fail("card not ready");
        return false;
    }
    const auto* in = static_cast<const std::uint8_t*>(src);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!write_single(lba + i, in + static_cast<std::size_t>(i) * block_size)) {
            return false;
        }
    }
    return true;
}

}  // namespace mqpro::sd
