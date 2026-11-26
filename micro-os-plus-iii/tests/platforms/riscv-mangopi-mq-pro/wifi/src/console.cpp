// console.cpp - D1 UART0, PB8 = TX, PB9 = RX, 115200 8N1.
//
// On the D1 the UART baud clock is APB1 (D1-H user manual, UART chapter:
// "The clock of the UART is from APB1").  APB1 resets to HOSC / 1 = 24 MHz.
// The divisor and the resulting rate are computed in board.hpp; see the
// static_asserts there.
//
// The tiny formatter is split into constexpr pieces (spec parsing and digit
// generation) so its behaviour is verified at compile time, not only on the
// board.
#include "console.hpp"

#include <array>
#include <string_view>

#include "gpio.hpp"

namespace mqpro {

namespace {

constexpr std::uint32_t rx_pull_bits = 0x0f;  // PB8 + PB9

struct format_spec {
    std::uint32_t base{10};
    std::uint32_t width{0};
    char pad{' '};
    bool upper{false};
};

[[nodiscard]] constexpr format_spec parse_spec(std::string_view text) noexcept {
    format_spec spec{};
    for (const char c : text) {
        if (c >= '1' && c <= '9') {
            spec.width = spec.width * 10u + static_cast<std::uint32_t>(c - '0');
        } else if (c == '0' && spec.width == 0) {
            spec.pad = '0';
        } else if (c == 'x') {
            spec.base = 16;
        } else if (c == 'X') {
            spec.base = 16;
            spec.upper = true;
        } else if (c == 'b') {
            spec.base = 2;
        } else if (c == 'o') {
            spec.base = 8;
        } else if (c == 'd') {
            spec.base = 10;
        }
    }
    return spec;
}

struct formatted_number {
    std::array<char, 64> digits{};
    std::size_t begin{64};
    std::size_t length{0};

    [[nodiscard]] constexpr std::string_view text() const noexcept {
        return {digits.data() + begin, length};
    }
};

[[nodiscard]] constexpr formatted_number format_number(std::uint64_t value,
                                                       std::uint32_t base,
                                                       bool upper) noexcept {
    constexpr std::string_view lower_digits{"0123456789abcdef"};
    constexpr std::string_view upper_digits{"0123456789ABCDEF"};
    const auto digits = upper ? upper_digits : lower_digits;

    formatted_number out{};
    std::size_t position = out.digits.size();
    do {
        out.digits[--position] = digits[static_cast<std::size_t>(value % base)];
        value /= base;
    } while (value != 0);

    out.begin = position;
    out.length = out.digits.size() - position;
    return out;
}

// Compile-time checks of the formatter (and of board.hpp's clock maths).
static_assert(format_number(0, 10, false).text() == "0");
static_assert(format_number(115'200, 10, false).text() == "115200");
static_assert(format_number(0x4000'0000u, 16, false).text() == "40000000");
static_assert(format_number(0xffu, 16, true).text() == "FF");
static_assert(format_number(6, 2, false).text() == "110");

static_assert(parse_spec("").base == 10);
static_assert(parse_spec("").width == 0);
static_assert(parse_spec("016x").base == 16);
static_assert(parse_spec("016x").width == 16);
static_assert(parse_spec("016x").pad == '0');
static_assert(parse_spec("X").upper);
static_assert(parse_spec("b").base == 2);
static_assert(parse_spec("o").base == 8);

}  // namespace

void console::init() const noexcept {
    // 1. UART0 is clocked from APB1 = HOSC / 1 (M = 0, N = 0, source = HOSC).
    mmio::reg32{board::ccu_base + board::ccu_apb1_clk}.write(0);

    // 2. PB8/PB9 to UART0.
    gpio::set_function(board::uart0_tx, board::mux::uart0);
    gpio::set_function(board::uart0_rx, board::mux::uart0);

    // 3. Pull-up on the RX line (harmless on TX).
    mmio::reg32{board::pull_addr(board::uart0_rx.port, board::uart0_rx.index)}
        .clear_set(rx_pull_bits,
                   (static_cast<unsigned>(board::pull::up) << 0) |
                       (static_cast<unsigned>(board::pull::up) << 2));

    // 4. Clock the bus and release the UART0 reset.
    mmio::reg32{board::ccu_base + board::ccu_uart_bgr}.write((1u << 0) | (1u << 16));
    for (int i = 0; i < 10'000; ++i) {
        __asm__ volatile("nop");
    }

    // 5. 115200 8N1, FIFO on.
    reg(board::uart_ier).write(0x00);
    reg(board::uart_lcr).write(board::uart_lcr_dlab);
    reg(board::uart_dll).write(board::uart_divisor);
    reg(board::uart_dlh).write(0x00);
    reg(board::uart_lcr).write(board::uart_lcr_8n1);
    reg(board::uart_fcr).write(board::uart_fcr_enable | board::uart_fcr_rx_reset |
                               board::uart_fcr_tx_reset);

    // Drain whatever the boot ROM / xfel left behind.
    while ((reg(board::uart_lsr).read() & board::uart_lsr_dr) != 0) {
        (void)reg(board::uart_rbr).read();
    }
}

void console::put(char c) const noexcept {
    while ((reg(board::uart_lsr).read() & board::uart_lsr_thre) == 0) {
    }
    reg(board::uart_thr).write(static_cast<std::uint8_t>(c));

    if (mirror_ != nullptr) {
        mirror_(c);
    }
}

void console::write(std::string_view text) const noexcept {
    for (char c : text) {
        put(c);
    }
}

std::optional<char> console::get() const noexcept {
    if ((reg(board::uart_lsr).read() & board::uart_lsr_dr) == 0) {
        return std::nullopt;
    }
    return static_cast<char>(reg(board::uart_rbr).read() & 0xffu);
}

void console::emit_number(std::uint64_t value, std::uint32_t base,
                          std::uint32_t width, char pad, bool upper) const noexcept {
    const auto number = format_number(value, base, upper);
    const auto text = number.text();

    for (auto i = static_cast<std::uint32_t>(text.size()); i < width; ++i) {
        put(pad);
    }
    write(text);
}

void console::emit_unsigned(std::string_view spec, std::uint64_t value) const noexcept {
    const auto parsed = parse_spec(spec);
    emit_number(value, parsed.base, parsed.width, parsed.pad, parsed.upper);
}

void console::emit_signed(std::string_view spec, std::int64_t value) const noexcept {
    if (value < 0) {
        put('-');
        emit_unsigned(spec, static_cast<std::uint64_t>(-(value + 1)) + 1u);
    } else {
        emit_unsigned(spec, static_cast<std::uint64_t>(value));
    }
}

}  // namespace mqpro
