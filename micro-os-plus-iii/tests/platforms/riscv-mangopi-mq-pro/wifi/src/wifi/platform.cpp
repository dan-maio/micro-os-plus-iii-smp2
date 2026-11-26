// platform.cpp - time, delays, kprintf, console log capture and the SDIO
// entry points used by the C code under wifi/rtw.
#include "platform.hpp"

#include <array>
#include <cstdarg>

#include <cmsis-plus/rtos/os.h>

#include "d1_board.hpp"
#include "console.hpp"
#include "sdio.hpp"

namespace mqpro::platform {

namespace {

constinit void (*idle_hook)() = nullptr;

constexpr std::size_t log_capacity = 128u * 1024u;   // power of two
constinit std::array<char, log_capacity> log_ring{};
constinit std::size_t log_head = 0;   // next write
constinit std::size_t log_tail = 0;   // next read

[[nodiscard]] std::uint64_t ticks() noexcept {
    std::uint64_t value;
    __asm__ volatile("rdtime %0" : "=r"(value));
    return value;
}

constexpr std::uint64_t ticks_per_us = board::timer_hz / 1'000'000u;

void put(char c) noexcept {
    if (c == '\n') {
        serial.put('\r');
    }
    serial.put(c);
}

void put_number(std::uint64_t value, unsigned base, int width, char pad,
                bool upper, bool negative) noexcept {
    char digits[24];
    int count = 0;
    do {
        const unsigned digit = static_cast<unsigned>(value % base);
        digits[count++] = static_cast<char>(
            digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + digit - 10);
        value /= base;
    } while (value != 0);
    int length = count + (negative ? 1 : 0);
    if (negative && pad == '0') {
        put('-');
    }
    for (; length < width; ++length) {
        put(pad);
    }
    if (negative && pad != '0') {
        put('-');
    }
    while (count > 0) {
        put(digits[--count]);
    }
}

}  // namespace

std::uint32_t millis() noexcept {
    return static_cast<std::uint32_t>(ticks() / (board::timer_hz / 1000u));
}

void set_idle_hook(void (*hook)()) noexcept { idle_hook = hook; }

void log_capture(char c) noexcept {
    log_ring[log_head & (log_capacity - 1u)] = c;
    ++log_head;
    if (log_head - log_tail > log_capacity) {
        log_tail = log_head - log_capacity;   // drop the oldest text
    }
}

std::size_t log_size() noexcept { return log_head - log_tail; }

std::size_t log_read(std::span<char> out) noexcept {
    std::size_t count = 0;
    while (count < out.size() && log_tail != log_head) {
        out[count++] = log_ring[log_tail & (log_capacity - 1u)];
        ++log_tail;
    }
    return count;
}

}  // namespace mqpro::platform

using namespace mqpro;

extern "C" std::uint64_t plat_time_us(void) {
    return platform::ticks() / platform::ticks_per_us;
}

extern "C" void plat_delay_us(std::uint32_t us) {
    // Under the RTOS a driver delay is a scheduling point, not a spin: the
    // chip waits are milliseconds long (power-up, calibration, EAPOL retries)
    // and the rest of the system -- the SD monitor, the console -- must run.
    // Sub-millisecond delays stay busy-waits, because they are timing
    // constraints of the bus (SDIO command/response gaps).
    if (us >= 1000u) {
        const std::uint32_t ms = (us + 999u) / 1000u;
        os::rtos::sysclock.sleep_for(ms);
        return;
    }
    const std::uint64_t start = platform::ticks();
    const std::uint64_t wanted = static_cast<std::uint64_t>(us) * platform::ticks_per_us;
    while (platform::ticks() - start < wanted) {
    }
}

// Minimal printf: %c %s %d %i %u %x %X %p %%, flags 0, width, l/ll/z/h.
extern "C" void kprintf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    for (const char* p = format; *p != '\0'; ++p) {
        if (*p != '%') {
            platform::put(*p);
            continue;
        }
        ++p;
        char pad = ' ';
        int width = 0;
        int longs = 0;
        bool left = false;
        if (*p == '-') {
            left = true;
            ++p;
        }
        if (*p == '0') {
            pad = '0';
            ++p;
        }
        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            ++p;
        }
        while (*p == 'l' || *p == 'z' || *p == 'h') {
            if (*p == 'l' || *p == 'z') {
                ++longs;
            }
            ++p;
        }
        switch (*p) {
        case 'c':
            platform::put(static_cast<char>(va_arg(args, int)));
            break;
        case 's': {
            const char* text = va_arg(args, const char*);
            if (text == nullptr) {
                text = "(null)";
            }
            int length = 0;
            while (text[length] != '\0') {
                ++length;
            }
            if (!left) {
                for (int i = length; i < width; ++i) platform::put(' ');
            }
            for (int i = 0; i < length; ++i) platform::put(text[i]);
            if (left) {
                for (int i = length; i < width; ++i) platform::put(' ');
            }
            break;
        }
        case 'd':
        case 'i': {
            const std::int64_t value = longs != 0 ? va_arg(args, long) : va_arg(args, int);
            const bool negative = value < 0;
            const std::uint64_t magnitude =
                negative ? static_cast<std::uint64_t>(-(value + 1)) + 1u
                         : static_cast<std::uint64_t>(value);
            platform::put_number(magnitude, 10, width, pad, false, negative);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            const std::uint64_t value =
                longs != 0 ? va_arg(args, unsigned long) : va_arg(args, unsigned int);
            platform::put_number(value, *p == 'u' ? 10 : 16, width, pad, *p == 'X', false);
            break;
        }
        case 'p':
            platform::put('0');
            platform::put('x');
            platform::put_number(reinterpret_cast<std::uintptr_t>(va_arg(args, void*)),
                                 16, 0, ' ', false, false);
            break;
        case '%':
            platform::put('%');
            break;
        case '\0':
            --p;
            break;
        default:
            platform::put('%');
            platform::put(*p);
            break;
        }
    }
    va_end(args);
}

// ---- SDIO for the C driver: 0 on success, negative on error ---------------
extern "C" int plat_sdio_readb(std::uint32_t addr, std::uint8_t* value) {
    return sdio::read_byte(1, addr, *value) ? 0 : -5;
}

extern "C" int plat_sdio_writeb(std::uint32_t addr, std::uint8_t value) {
    return sdio::write_byte(1, addr, value) ? 0 : -5;
}

extern "C" int plat_sdio_read(std::uint32_t addr, void* buf, std::size_t len) {
    return sdio::read(addr, buf, len) ? 0 : -5;
}

extern "C" int plat_sdio_write(std::uint32_t addr, const void* buf, std::size_t len) {
    return sdio::write(addr, buf, len) ? 0 : -5;
}

extern "C" std::size_t plat_sdio_align_size(std::size_t len) {
    return sdio::align_size(len);
}
