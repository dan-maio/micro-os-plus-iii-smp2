// gpio.hpp - pin multiplexing and level control for the D1 PIO block.
#pragma once

#include <cstdint>
#include <utility>

#include "d1_board.hpp"
#include "mmio.hpp"

namespace mqpro::gpio {

namespace detail {
inline constexpr std::uint32_t cfg_shift(board::pin p) noexcept {
    return (p.index % 8u) * 4u;
}
inline constexpr std::uint32_t pull_shift(board::pin p) noexcept {
    return (p.index % 8u) * 2u;
}
}  // namespace detail

inline void set_function(board::pin p, board::mux function) noexcept {
    const auto shift = detail::cfg_shift(p);
    mmio::reg32{board::cfg_addr(p.port, p.index)}.clear_set(
        0xFu << shift, static_cast<unsigned>(function) << shift);
}

inline void set_pull(board::pin p, board::pull value) noexcept {
    const auto shift = detail::pull_shift(p);
    mmio::reg32{board::pull_addr(p.port, p.index)}.clear_set(
        0x3u << shift, static_cast<unsigned>(value) << shift);
}

inline void set_output(board::pin p) noexcept {
    set_function(p, board::mux::output);
    set_pull(p, board::pull::none);
}

inline void write(board::pin p, bool high) noexcept {
    mmio::reg32 reg{board::dat_addr(p.port)};
    if (high) {
        reg.set(1u << p.index);
    } else {
        reg.clear(1u << p.index);
    }
}

inline void toggle(board::pin p) noexcept {
    mmio::reg32 reg{board::dat_addr(p.port)};
    reg.write(reg.read() ^ (1u << p.index));
}

[[nodiscard]] inline bool read(board::pin p) noexcept {
    return (mmio::reg32{board::dat_addr(p.port)}.read() >> p.index) & 1u;
}

}  // namespace mqpro::gpio
