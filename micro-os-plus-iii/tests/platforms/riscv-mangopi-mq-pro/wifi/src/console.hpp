// console.hpp - UART0 console with a small compile-time-checked formatter.
//
// Usage:
//     serial.print("[S] satp = 0x{:016x}\r\n", satp);
//     serial.print("[S] uptime {} s, irq {}\r\n", seconds, ticks);
//
// Supported format specs: {} (decimal), {:d}, {:x}, {:X}, {:b}, {:o},
// with an optional zero-padded width such as {:08x}.
#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include "d1_board.hpp"
#include "mmio.hpp"

namespace mqpro {

namespace detail {
template <typename>
inline constexpr bool dependent_false = false;
}

class console {
public:
    /// Optional secondary sink, e.g. the USB CDC mirror.
    using mirror_fn = void (*)(char);

    constexpr explicit console(std::uintptr_t base) noexcept : base_{base} {}

    void set_mirror(mirror_fn fn) noexcept { mirror_ = fn; }

    void init() const noexcept;
    void put(char c) const noexcept;
    void write(std::string_view text) const noexcept;

    /// Next received byte, or std::nullopt when the FIFO is empty.
    [[nodiscard]] std::optional<char> get() const noexcept;

    template <typename... Args>
    void print(std::string_view format, const Args&... args) const {
        print_args(format, args...);
    }

private:
    [[nodiscard]] mmio::reg32 reg(std::uint32_t offset) const noexcept {
        return mmio::reg32{base_ + offset};
    }

    void print_args(std::string_view format) const { write(format); }

    // Finds the next {} placeholder without <string_view>::find/substr: those
    // pull in memchr and the checked-subscript assertion, i.e. more runtime
    // than a freestanding image wants.
    template <typename T, typename... Rest>
    void print_args(std::string_view format, const T& value, const Rest&... rest) const {
        std::size_t open = 0;
        while (open < format.size() && format[open] != '{') {
            ++open;
        }
        std::size_t close = open + 1;
        while (close < format.size() && format[close] != '}') {
            ++close;
        }
        if (open == format.size() || close == format.size()) {
            write(format);
            return;
        }
        write(std::string_view{format.data(), open});
        emit(std::string_view{format.data() + open + 1, close - open - 1}, value);
        print_args(std::string_view{format.data() + close + 1, format.size() - close - 1},
                   rest...);
    }

    template <typename T>
    void emit(std::string_view spec, const T& value) const noexcept {
        if constexpr (std::same_as<T, std::string_view>) {
            write(value);
        } else if constexpr (std::same_as<std::remove_cvref_t<T>, const char*> ||
                             std::same_as<std::remove_cvref_t<T>, char*>) {
            write(std::string_view{value});
        } else if constexpr (std::same_as<T, char>) {
            (void)spec;
            put(value);
        } else if constexpr (std::same_as<T, bool>) {
            (void)spec;
            write(value ? std::string_view{"true"} : std::string_view{"false"});
        } else if constexpr (std::unsigned_integral<T>) {
            emit_unsigned(spec, static_cast<std::uint64_t>(value));
        } else if constexpr (std::integral<T>) {
            emit_signed(spec, static_cast<std::int64_t>(value));
        } else {
            static_assert(detail::dependent_false<T>,
                          "console::print: unsupported argument type");
        }
    }

    void emit_unsigned(std::string_view spec, std::uint64_t value) const noexcept;
    void emit_signed(std::string_view spec, std::int64_t value) const noexcept;
    void emit_number(std::uint64_t value, std::uint32_t base, std::uint32_t width,
                     char pad, bool upper) const noexcept;

    std::uintptr_t base_;
    mirror_fn mirror_{nullptr};
};

/// The single console on UART0 (PB8 = TX, PB9 = RX).
inline constinit console serial{board::uart0_base};

}  // namespace mqpro
