/*
 * riscv-board.hpp -- Raspberry Pi Pico 2 (RP2350), the two Hazard3 harts.
 *
 * Not a CLINT: the RP2350 keeps the RISC-V platform timer and the software
 * interrupt in its SIO block (datasheet 3.1.8):
 *   MTIME/MTIMEH        0xD00001B0  one 64-bit counter, shared by both cores
 *   MTIMECMP/MTIMECMPH  0xD00001B8  per core, at the SAME address (stride 0)
 *   RISCV_SOFTIRQ       0xD00001A0  bit h sets core h's MSIP, bit 8+h clears
 * Hazard3 does not implement the time/timeh CSRs (datasheet, MCOUNTEREN.TM),
 * so the port reads MTIME over MMIO (TIME_CSR = false). The timer counts the
 * TICKS block's RISC-V tick: 1 MHz from the 12 MHz crystal (board.cpp).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <rp2350.hpp>

namespace riscv_board
{
  inline constexpr std::uintptr_t CLINT_MTIME = rp2350::SIO_MTIME;
  inline constexpr std::uintptr_t CLINT_MTIMECMP = rp2350::SIO_MTIMECMP;
  inline constexpr std::uintptr_t CLINT_MTIMECMP_STRIDE = 0u; // core-local
  // Not used (RISCV_BOARD_SOFTIRQ_SIO below); kept so clint.hpp compiles.
  inline constexpr std::uintptr_t CLINT_MSIP = rp2350::SIO_RISCV_SOFTIRQ;
  inline constexpr std::uintptr_t CLINT_MSIP_STRIDE = 0u;
  // 32-bit registers: 64-bit values are written as halves, in the safe order.
  inline constexpr bool MTIMECMP_SPLIT32 = true;
  inline constexpr bool MTIME_READABLE = true;
  inline constexpr bool TIME_CSR = false;

  inline constexpr std::uint32_t TIMEBASE_HZ = 1000000u;
} // namespace riscv_board

// The software interrupt is SIO RISCV_SOFTIRQ, not a CLINT msip word per hart
// (clint.hpp uses these instead).
#define RISCV_BOARD_SOFTIRQ_SIO 1

extern "C"
{
  void riscv_board_initialize_early (void);
  void riscv_board_initialize (void);
  void riscv_board_console_write (const char* s, std::size_t n);
}
