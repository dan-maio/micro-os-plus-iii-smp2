// board.hpp - Allwinner D1 / MangoPi MQ-Pro memory map and register bits.
//
// Values from the D1-H user manual (v1.0) and the MangoPi MQ-Pro schematic.
// Everything is constexpr so register addresses and clock maths are resolved
// at compile time.
#pragma once

#include <cstdint>
#include <cstddef>

namespace mqpro::board {

// ---------------------------------------------------------------- memory map
inline constexpr std::uintptr_t dram_base  = 0x4000'0000;  // DDR / load address
inline constexpr std::uintptr_t gpio_base  = 0x0200'0000;
inline constexpr std::uintptr_t ccu_base   = 0x0200'1000;
inline constexpr std::uintptr_t uart0_base = 0x0250'0000;
inline constexpr std::uintptr_t clint_base = 0x1400'0000;

// ---------------------------------------------------------------------- CCU
inline constexpr std::uint32_t ccu_apb1_clk = 0x0524;  // APB1 clock (UART source)
inline constexpr std::uint32_t ccu_uart_bgr = 0x090c;  // UART bus gating / reset

// --------------------------------------------------------------------- GPIO
// A port occupies 0x30 bytes: CFG0..CFG3 at +0x00, DAT at +0x10,
// PULL0.. at +0x24.  One CFG nibble per pin, one PULL field per 8 pins
// (this matches the working register usage of the original C demo).
inline constexpr std::uint32_t port_b = 0x0030;
inline constexpr std::uint32_t port_d = 0x0090;

inline constexpr std::uintptr_t cfg_addr(std::uint32_t port, std::uint32_t pin) noexcept {
    return gpio_base + port + (pin / 8u) * 4u;
}
inline constexpr std::uintptr_t dat_addr(std::uint32_t port) noexcept {
    return gpio_base + port + 0x10u;
}
inline constexpr std::uintptr_t pull_addr(std::uint32_t port, std::uint32_t pin) noexcept {
    return gpio_base + port + 0x24u + (pin / 8u) * 4u;
}

// --------------------------------------------------------------------- pins
struct pin {
    std::uint32_t port;  // port_b / port_d
    std::uint32_t index;
};

inline constexpr pin led_pin   {port_d, 18};  // onboard blue LED, active high
inline constexpr pin uart0_tx  {port_b, 8};
inline constexpr pin uart0_rx  {port_b, 9};

enum class mux : std::uint32_t {
    input  = 0x0,
    output = 0x1,
    uart0  = 0x6,   // PB8 = UART0_TX, PB9 = UART0_RX
};

enum class pull : std::uint32_t {
    none = 0x0,
    up   = 0x1,
    down = 0x2,
};

// --------------------------------------------------------------------- UART
// 16550-compatible register offsets.
inline constexpr std::uint32_t uart_rbr = 0x00;
inline constexpr std::uint32_t uart_thr = 0x00;
inline constexpr std::uint32_t uart_dll = 0x00;
inline constexpr std::uint32_t uart_ier = 0x04;
inline constexpr std::uint32_t uart_dlh = 0x04;
inline constexpr std::uint32_t uart_fcr = 0x08;
inline constexpr std::uint32_t uart_lcr = 0x0c;
inline constexpr std::uint32_t uart_lsr = 0x14;

inline constexpr std::uint32_t uart_lsr_dr   = 0x01;  // data ready
inline constexpr std::uint32_t uart_lsr_thre = 0x20;  // THR empty

inline constexpr std::uint32_t uart_lcr_8n1  = 0x03;
inline constexpr std::uint32_t uart_lcr_dlab = 0x80;

inline constexpr std::uint32_t uart_fcr_enable   = 0x01;
inline constexpr std::uint32_t uart_fcr_rx_reset = 0x02;
inline constexpr std::uint32_t uart_fcr_tx_reset = 0x04;

// ------------------------------------------------------------------- clocks
inline constexpr std::uint32_t apb1_hz         = 24'000'000;  // HOSC, M=1 N=1
inline constexpr std::uint32_t baud            = 115'200;
inline constexpr std::uint32_t uart_oversample = 16;

// The D1 UART has an integer divisor only:
//   24e6 / (16 * 115200) = 13.02 -> 13, i.e. 115384 Bd (+0.16 %).
inline constexpr std::uint32_t uart_divisor =
    apb1_hz / (uart_oversample * baud);
inline constexpr std::uint32_t uart_actual_baud =
    apb1_hz / (uart_oversample * uart_divisor);

static_assert(uart_divisor > 0, "baud rate too high for APB1");
static_assert(uart_actual_baud == 115'384, "unexpected UART divisor");

// ------------------------------------------------------------- machine timer
inline constexpr std::uint32_t clint_mtimecmp0 = 0x4000;  // offset from CLINT
inline constexpr std::uint64_t timer_irq_ticks = 12'000'000;  // 0.5 s at 24 MHz
inline constexpr std::uint64_t timer_hz        = 24'000'000;
inline constexpr std::uint64_t heartbeat_ticks = 20 * timer_hz;

}  // namespace mqpro::board
