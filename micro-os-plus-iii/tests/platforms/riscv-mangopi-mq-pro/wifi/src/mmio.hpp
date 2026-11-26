// mmio.hpp - type-safe memory-mapped register access.
//
// A register is just an address plus an integer width; every access is a
// single volatile load or store, so nothing is ever cached in a plain member.
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace mqpro::mmio {

template <std::integral T>
    requires(!std::is_const_v<T>)
class reg {
public:
    constexpr explicit reg(std::uintptr_t address) noexcept : address_{address} {}

    [[nodiscard]] T read() const noexcept {
        return *reinterpret_cast<volatile T*>(address_);
    }

    void write(T value) const noexcept {
        *reinterpret_cast<volatile T*>(address_) = value;
    }

    void set(T bits) const noexcept { write(read() | bits); }
    void clear(T bits) const noexcept { write(read() & static_cast<T>(~bits)); }

    void clear_set(T clear_mask, T set_mask) const noexcept {
        write(static_cast<T>((read() & static_cast<T>(~clear_mask)) | set_mask));
    }

    [[nodiscard]] constexpr std::uintptr_t address() const noexcept { return address_; }

private:
    std::uintptr_t address_;
};

using reg8  = reg<std::uint8_t>;
using reg32 = reg<std::uint32_t>;
using reg64 = reg<std::uint64_t>;

// Compiler barrier for MMIO ordering; the SoC has a single hart here, but the
// compiler must not reorder device accesses around these points.
inline void barrier() noexcept {
    __asm__ volatile("" ::: "memory");
}

}  // namespace mqpro::mmio
