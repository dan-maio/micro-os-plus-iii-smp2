// platform.hpp - services shared by the C parts of the network stack (the
// rtw88 port, lwIP) and the C++ application: time, delays, a printf-style
// logger and the captured console log.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" {
std::uint64_t plat_time_us(void);
void plat_delay_us(std::uint32_t us);
void kprintf(const char* format, ...) __attribute__((format(printf, 1, 2)));
}

namespace mqpro::platform {

/// Milliseconds since boot.
[[nodiscard]] std::uint32_t millis() noexcept;

/// Hook called while long delays run, so USB stays serviced.
void set_idle_hook(void (*hook)()) noexcept;

/// Everything printed on the console is also kept in a RAM ring, so a host
/// without access to UART0 can fetch it over USB ("log" command).
void log_capture(char c) noexcept;
[[nodiscard]] std::size_t log_size() noexcept;
std::size_t log_read(std::span<char> out) noexcept;   // consumes

}  // namespace mqpro::platform
