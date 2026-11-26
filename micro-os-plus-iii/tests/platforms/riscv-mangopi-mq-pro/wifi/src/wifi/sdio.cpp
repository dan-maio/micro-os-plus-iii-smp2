// sdio.cpp - D1 SDC1 SDIO host (PIO, 4-bit, 25 MHz) for the RTL8723DS.
//
// Same controller and the same four bring-up rules as sd/sd_card.cpp:
// HOSC-sourced module clock, no CRC on R4 (like R3), GCTRL.ACCESS_BY_AHB for
// PIO.  SDIO needs no CMD2/ACMD41 and no stop command: CMD53 carries its own
// byte or block count.
#include "sdio.hpp"

#include "d1_board.hpp"
#include "gpio.hpp"
#include "mmio.hpp"

namespace mqpro::sdio {

namespace {

constexpr std::uintptr_t sdc_base = 0x0402'1000u;   // SDC1
constexpr std::uintptr_t ccu_base = 0x0200'1000u;

constexpr std::uint32_t ccu_bus_mmc = 0x084cu;   // bit1 gate, bit17 reset (MMC1)
constexpr std::uint32_t ccu_mmc1_clk = 0x0834u;

constexpr std::uint32_t r_gctrl = 0x000;
constexpr std::uint32_t r_clkcr = 0x004;
constexpr std::uint32_t r_tmout = 0x008;
constexpr std::uint32_t r_width = 0x00c;
constexpr std::uint32_t r_blksz = 0x010;
constexpr std::uint32_t r_bcntr = 0x014;
constexpr std::uint32_t r_cmdr = 0x018;
constexpr std::uint32_t r_carg = 0x01c;
constexpr std::uint32_t r_resp0 = 0x020;
constexpr std::uint32_t r_imask = 0x030;
constexpr std::uint32_t r_rintr = 0x038;
constexpr std::uint32_t r_stas = 0x03c;
constexpr std::uint32_t r_ftrgl = 0x040;
constexpr std::uint32_t r_cbcr = 0x048;
constexpr std::uint32_t r_fifo = 0x200;

constexpr std::uint32_t gc_soft_reset = 1u << 0;
constexpr std::uint32_t gc_fifo_reset = 1u << 1;
constexpr std::uint32_t gc_dma_reset = 1u << 2;
constexpr std::uint32_t gc_int_enable = 1u << 4;
constexpr std::uint32_t gc_access_by_ahb = 1u << 31;

constexpr std::uint32_t clk_card_on = 1u << 16;
constexpr std::uint32_t width_4bit = 1u;

constexpr std::uint32_t cmd_resp_expire = 1u << 6;
constexpr std::uint32_t cmd_check_crc = 1u << 8;
constexpr std::uint32_t cmd_data_expire = 1u << 9;
constexpr std::uint32_t cmd_write = 1u << 10;
constexpr std::uint32_t cmd_wait_pre_over = 1u << 13;
constexpr std::uint32_t cmd_init_sequence = 1u << 15;
constexpr std::uint32_t cmd_upclk_only = 1u << 21;
constexpr std::uint32_t cmd_start = 1u << 31;

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
constexpr std::uint32_t ir_end_bit = 1u << 15;

constexpr std::uint32_t ir_error = ir_resp_error | ir_resp_crc | ir_data_crc |
                                   ir_resp_timeout | ir_data_timeout |
                                   ir_fifo_run | ir_hw_locked | ir_start_bit |
                                   ir_end_bit;

constexpr std::uint32_t st_fifo_empty = 1u << 2;
constexpr std::uint32_t st_fifo_full = 1u << 3;

// GPIO port G: 6 * 0x30.
constexpr std::uint32_t port_g = 0x0120u;
constexpr board::pin wl_reg_on{port_g, 17};
constexpr board::pin bt_enable{port_g, 15};

// SDIO CCCR (function 0) registers.
constexpr std::uint32_t cccr_revision = 0x00;
constexpr std::uint32_t cccr_io_enable = 0x02;
constexpr std::uint32_t cccr_io_ready = 0x03;
constexpr std::uint32_t cccr_bus_interface = 0x07;
constexpr std::uint32_t cccr_cis_pointer = 0x09;
constexpr std::uint32_t cccr_fn0_block_size = 0x10;
constexpr std::uint32_t fbr1_block_size = 0x110;

constinit card_info card{};
constinit bool card_ready = false;
constinit const char* error_text = "not initialised";
constinit std::uint32_t error_status = 0;

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

constexpr std::uint64_t ticks_per_ms = board::timer_hz / 1000u;

void delay_ms(std::uint32_t ms) noexcept {
    const std::uint64_t start = ticks();
    while (ticks() - start < ms * ticks_per_ms) {
    }
}

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

bool fail(const char* why, std::uint32_t status = 0) noexcept {
    error_text = why;
    error_status = status;
    return false;
}

void reset_fifo() noexcept {
    wr(r_gctrl, rd(r_gctrl) | gc_fifo_reset);
    for (int i = 0; i < 1000 && (rd(r_gctrl) & gc_fifo_reset) != 0; ++i) {
    }
}

// After a failed data command the controller state machine may still be
// waiting for data: reset it but keep clock and bus width.
void recover() noexcept {
    const auto clkcr = rd(r_clkcr);
    const auto width = rd(r_width);
    wr(r_gctrl, gc_soft_reset | gc_fifo_reset | gc_dma_reset);
    for (int i = 0; i < 100'000 && (rd(r_gctrl) & gc_soft_reset) != 0; ++i) {
    }
    wr(r_gctrl, gc_int_enable | gc_access_by_ahb);
    wr(r_imask, 0);
    wr(r_tmout, 0xffffffffu);
    wr(r_ftrgl, 16);
    wr(r_width, width);
    wr(r_clkcr, clkcr);
    wr(r_cmdr, cmd_start | cmd_upclk_only | cmd_wait_pre_over);
    for (int i = 0; i < 100'000 && (rd(r_cmdr) & cmd_start) != 0; ++i) {
    }
    wr(r_rintr, 0xffffffffu);
}

bool send_command(std::uint32_t index, std::uint32_t argument,
                  std::uint32_t flags, std::uint32_t* response) noexcept {
    wr(r_rintr, 0xffffffffu);
    wr(r_carg, argument);
    wr(r_cmdr, flags | (index & 0x3fu) | cmd_start);

    const std::uint64_t deadline = ticks() + 100u * ticks_per_ms;
    for (;;) {
        const std::uint32_t status = rd(r_rintr);
        if ((status & ir_error) != 0) {
            wr(r_rintr, status);
            return fail(describe(status), status);
        }
        if ((status & ir_cmd_done) != 0) {
            break;
        }
        if (ticks() > deadline) {
            return fail("command timeout");
        }
    }
    if (response != nullptr) {
        *response = rd(r_resp0);
    }
    wr(r_rintr, ir_cmd_done);
    return true;
}

bool set_clock(std::uint32_t target_hz) noexcept {
    wr(r_clkcr, rd(r_clkcr) & ~clk_card_on);
    wr(r_cmdr, cmd_start | cmd_upclk_only | cmd_wait_pre_over);
    for (int i = 0; i < 100'000 && (rd(r_cmdr) & cmd_start) != 0; ++i) {
    }

    // Module clock = 24 MHz HOSC / (m * 2^n); the controller divides by 2*div
    // (div = 0 means bypass).
    constexpr std::uint32_t parent_hz = 24'000'000;
    mmio::reg32{ccu_base + ccu_mmc1_clk}.write(1u << 31);   // HOSC, m = 1, n = 0

    std::uint32_t divider = 0;
    std::uint32_t actual = parent_hz;
    if (target_hz < parent_hz) {
        divider = (parent_hz + 2u * target_hz - 1u) / (2u * target_hz);
        if (divider > 255u) {
            divider = 255u;
        }
        actual = parent_hz / (2u * divider);
    }

    std::uint32_t clkcr = rd(r_clkcr) & ~0xffu;
    clkcr |= divider;
    wr(r_clkcr, clkcr);
    wr(r_cmdr, cmd_start | cmd_upclk_only | cmd_wait_pre_over);
    for (int i = 0; i < 100'000 && (rd(r_cmdr) & cmd_start) != 0; ++i) {
    }
    wr(r_clkcr, clkcr | clk_card_on);
    wr(r_cmdr, cmd_start | cmd_upclk_only | cmd_wait_pre_over);
    for (int i = 0; i < 100'000 && (rd(r_cmdr) & cmd_start) != 0; ++i) {
    }
    wr(r_rintr, 0xffffffffu);

    card.clock_hz = actual;
    return true;
}

bool cmd52(bool write_flag, std::uint32_t function, std::uint32_t address,
           std::uint8_t in, std::uint8_t* out) noexcept {
    const std::uint32_t argument = (write_flag ? 1u << 31 : 0u) |
                                   ((function & 7u) << 28) |
                                   ((address & 0x1ffffu) << 9) | in;
    std::uint32_t response = 0;
    if (!send_command(52, argument, cmd_resp_expire | cmd_check_crc, &response)) {
        return false;
    }
    // R5: bits 15:8 are flags (COM_CRC, ILLEGAL, ERROR, FUNCTION, RANGE).
    if ((response & 0xcb00u) != 0) {
        return fail("cmd52 R5 error", response);
    }
    if (out != nullptr) {
        *out = static_cast<std::uint8_t>(response);
    }
    return true;
}

bool wait_data_over() noexcept {
    const std::uint64_t deadline = ticks() + 1000u * ticks_per_ms;
    for (;;) {
        const std::uint32_t status = rd(r_rintr);
        if ((status & ir_error) != 0) {
            wr(r_rintr, status);
            return fail(describe(status), status);
        }
        if ((status & ir_data_over) != 0) {
            wr(r_rintr, ir_data_over | ir_rx_req | ir_tx_req);
            return true;
        }
        if (ticks() > deadline) {
            return fail("data timeout (host)");
        }
    }
}

// One CMD53: `count` is a byte count (byte mode) or a block count.
bool cmd53(bool write_flag, std::uint32_t address, std::uint32_t* words,
           bool block_mode, std::uint32_t count) noexcept {
    const std::uint32_t bytes = block_mode ? count * block_size : count;
    const std::uint32_t nwords = (bytes + 3u) / 4u;

    reset_fifo();
    wr(r_gctrl, rd(r_gctrl) | gc_access_by_ahb);
    wr(r_blksz, block_mode ? block_size : bytes);
    wr(r_bcntr, bytes);

    const std::uint32_t argument = (write_flag ? 1u << 31 : 0u) | (1u << 28) |
                                   (block_mode ? 1u << 27 : 0u) | (1u << 26) |
                                   ((address & 0x1ffffu) << 9) | (count & 0x1ffu);
    std::uint32_t flags = cmd_resp_expire | cmd_check_crc | cmd_data_expire |
                          cmd_wait_pre_over;
    if (write_flag) {
        flags |= cmd_write;
    }
    std::uint32_t response = 0;
    if (!send_command(53, argument, flags, &response)) {
        recover();
        return false;
    }
    if ((response & 0xcb00u) != 0) {
        recover();
        return fail("cmd53 R5 error", response);
    }

    for (std::uint32_t i = 0; i < nwords; ++i) {
        const std::uint64_t deadline = ticks() + 500u * ticks_per_ms;
        const std::uint32_t busy = write_flag ? st_fifo_full : st_fifo_empty;
        while ((rd(r_stas) & busy) != 0) {
            const std::uint32_t status = rd(r_rintr);
            if ((status & ir_error) != 0) {
                wr(r_rintr, status);
                recover();
                return fail(describe(status), status);
            }
            if (ticks() > deadline) {
                recover();
                return fail(write_flag ? "write fifo timeout" : "read fifo timeout");
            }
        }
        if (write_flag) {
            wr(r_fifo, words[i]);
        } else {
            words[i] = rd(r_fifo);
        }
    }
    if (!wait_data_over()) {
        recover();
        return false;
    }
    return true;
}

bool transfer(bool write_flag, std::uint32_t address, void* data,
              std::size_t length) noexcept {
    if (!card_ready) {
        return fail("sdio not ready");
    }
    auto* words = static_cast<std::uint32_t*>(data);
    if (length <= block_size) {
        const auto bytes = static_cast<std::uint32_t>((length + 3u) & ~std::size_t{3});
        // Byte mode: a count field of 0 means 512.
        return cmd53(write_flag, address, words, false, bytes);
    }
    std::uint32_t blocks = static_cast<std::uint32_t>((length + block_size - 1u) / block_size);
    while (blocks != 0) {
        const std::uint32_t chunk = blocks > 32u ? 32u : blocks;
        if (!cmd53(write_flag, address, words, true, chunk)) {
            return false;
        }
        // The TX/RX FIFO ports ignore the address; registers are never
        // accessed with more than one block.
        words += chunk * (block_size / 4u);
        blocks -= chunk;
    }
    return true;
}

bool read_cis() noexcept {
    std::uint8_t b0 = 0, b1 = 0, b2 = 0;
    if (!cmd52(false, 0, cccr_cis_pointer, 0, &b0) ||
        !cmd52(false, 0, cccr_cis_pointer + 1, 0, &b1) ||
        !cmd52(false, 0, cccr_cis_pointer + 2, 0, &b2)) {
        return false;
    }
    std::uint32_t pointer = b0 | (b1 << 8) | (b2 << 16);
    for (int guard = 0; guard < 64; ++guard) {
        std::uint8_t code = 0, link = 0;
        if (!cmd52(false, 0, pointer, 0, &code)) return false;
        if (code == 0xff) break;
        if (!cmd52(false, 0, pointer + 1, 0, &link)) return false;
        if (link == 0xff) break;
        if (code == 0x20 && link >= 4) {   // CISTPL_MANFID
            std::uint8_t id[4]{};
            for (std::uint32_t i = 0; i < 4; ++i) {
                if (!cmd52(false, 0, pointer + 2 + i, 0, &id[i])) return false;
            }
            card.vendor = static_cast<std::uint16_t>(id[0] | (id[1] << 8));
            card.device = static_cast<std::uint16_t>(id[2] | (id[3] << 8));
            return true;
        }
        pointer += 2u + link;
    }
    return true;   // no MANFID tuple: not fatal
}

}  // namespace

bool ready() noexcept { return card_ready; }
const card_info& info() noexcept { return card; }
const char* last_error() noexcept { return error_text; }
std::uint32_t last_status() noexcept { return error_status; }

bool init() noexcept {
    card_ready = false;
    card = {};

    // Bluetooth half stays disabled; WL_REG_ON low then high resets the WLAN.
    gpio::set_output(bt_enable);
    gpio::write(bt_enable, false);
    gpio::set_output(wl_reg_on);
    gpio::write(wl_reg_on, false);
    delay_ms(50);
    gpio::write(wl_reg_on, true);
    delay_ms(150);

    // PG0..PG5 = SDC1 (function 2), pull-ups on CMD and DAT.
    for (std::uint32_t i = 0; i < 6; ++i) {
        const board::pin pin{port_g, i};
        gpio::set_function(pin, static_cast<board::mux>(2));
        gpio::set_pull(pin, i == 0 ? board::pull::none : board::pull::up);
    }

    // CCU: gate + reset for MMC1 (bits 1 and 17).
    mmio::reg32{ccu_base + ccu_bus_mmc}.set(1u << 1);
    mmio::reg32{ccu_base + ccu_bus_mmc}.clear(1u << 17);
    delay_ms(1);
    mmio::reg32{ccu_base + ccu_bus_mmc}.set(1u << 17);
    mmio::reg32{ccu_base + ccu_mmc1_clk}.write(1u << 31);
    delay_ms(1);

    wr(r_gctrl, gc_soft_reset | gc_fifo_reset | gc_dma_reset);
    for (int i = 0; i < 100'000 &&
                    (rd(r_gctrl) & (gc_soft_reset | gc_fifo_reset | gc_dma_reset)) != 0; ++i) {
    }
    wr(r_gctrl, gc_int_enable | gc_access_by_ahb);
    wr(r_imask, 0);
    wr(r_tmout, 0xffffffffu);
    wr(r_ftrgl, 16);
    wr(r_cbcr, 0);
    wr(r_width, 0);

    if (!set_clock(400'000)) {
        return false;
    }

    std::uint32_t resp = 0;

    // CMD0 with the 80-clock init sequence, then CMD5 (R4, no CRC).
    (void)send_command(0, 0, cmd_init_sequence, nullptr);
    delay_ms(2);
    if (!send_command(5, 0, cmd_resp_expire, &resp)) {
        return false;
    }
    card.ocr = resp & 0x00ff'ffffu;
    card.functions = static_cast<std::uint8_t>((resp >> 28) & 7u);
    bool powered = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (!send_command(5, card.ocr & 0x0030'0000u, cmd_resp_expire, &resp)) {
            return false;
        }
        if ((resp & 0x8000'0000u) != 0) {
            powered = true;
            break;
        }
        delay_ms(5);
    }
    if (!powered) {
        return fail("CMD5: card never became ready");
    }

    if (!send_command(3, 0, cmd_resp_expire | cmd_check_crc, &resp)) {
        return false;
    }
    card.rca = (resp >> 16) & 0xffffu;
    if (!send_command(7, card.rca << 16, cmd_resp_expire | cmd_check_crc, &resp)) {
        return false;
    }

    std::uint8_t value = 0;
    if (!cmd52(false, 0, cccr_revision, 0, &value)) {
        return false;
    }
    card.cccr_version = value;

    // 4-bit bus, card-detect pull-up off.
    if (!cmd52(false, 0, cccr_bus_interface, 0, &value) ||
        !cmd52(true, 0, cccr_bus_interface,
               static_cast<std::uint8_t>((value & ~0x03u) | 0x02u | 0x80u), nullptr)) {
        return false;
    }
    wr(r_width, width_4bit);
    if (!set_clock(25'000'000)) {
        return false;
    }

    if (!read_cis()) {
        return false;
    }

    // 512-byte blocks for functions 0 and 1.
    if (!cmd52(true, 0, cccr_fn0_block_size, block_size & 0xffu, nullptr) ||
        !cmd52(true, 0, cccr_fn0_block_size + 1, block_size >> 8, nullptr) ||
        !cmd52(true, 0, fbr1_block_size, block_size & 0xffu, nullptr) ||
        !cmd52(true, 0, fbr1_block_size + 1, block_size >> 8, nullptr)) {
        return false;
    }

    // Enable function 1 and wait for it.
    if (!cmd52(true, 0, cccr_io_enable, 0x02, nullptr)) {
        return false;
    }
    bool enabled = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (!cmd52(false, 0, cccr_io_ready, 0, &value)) {
            return false;
        }
        if ((value & 0x02u) != 0) {
            enabled = true;
            break;
        }
        delay_ms(5);
    }
    if (!enabled) {
        return fail("function 1 never became ready");
    }

    card_ready = true;
    error_text = "";
    error_status = 0;
    return true;
}

bool read_byte(std::uint32_t function, std::uint32_t address, std::uint8_t& value) noexcept {
    return cmd52(false, function, address, 0, &value);
}

bool write_byte(std::uint32_t function, std::uint32_t address, std::uint8_t value) noexcept {
    return cmd52(true, function, address, value, nullptr);
}

bool read(std::uint32_t address, void* data, std::size_t length) noexcept {
    return transfer(false, address, data, length);
}

bool write(std::uint32_t address, const void* data, std::size_t length) noexcept {
    return transfer(true, address, const_cast<void*>(data), length);
}

}  // namespace mqpro::sdio
