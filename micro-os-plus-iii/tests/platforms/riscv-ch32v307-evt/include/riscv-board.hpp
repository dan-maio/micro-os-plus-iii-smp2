/*
 * riscv-board.hpp -- WCH CH32V307V-EVT-R1 (CH32V307VCT6, QingKe V4F).
 *
 * NOT a CLINT machine: the QingKe V4F has no mtime/mtimecmp and no machine
 * timer or software interrupt (mie/mip read 0; mcycle, cycle and time read
 * 0 -- measured with a CSR probe run from SRAM). Its interrupts go through
 * the PFIC (0xE000E000), and the port runs the PFIC path for it
 * (RISCV_PFIC, board.cmake; include/pfic.hpp).
 *
 * The tick is the QingKe SysTick, which has exactly the shape of CLINT's
 * timer: a 64-bit up-counter (STK_CNTL/CNTH, 0xE000F008) and a 64-bit
 * compare (STK_CMPLR/CMPHR, 0xE000F010), counting at HCLK (144 MHz;
 * board.cpp starts it). So the port's mtime/mtimecmp code drives it
 * unchanged: CLINT_MTIME = the counter, CLINT_MTIMECMP = the compare,
 * written as two 32-bit halves in the safe order. The interrupt it raises
 * is PFIC interrupt 12, not mip.MTIP (trap.cpp, RISCV_PFIC).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace riscv_board
{
  inline constexpr std::uintptr_t CLINT_MTIME = 0xE000F008u;    // STK_CNTL
  inline constexpr std::uintptr_t CLINT_MTIMECMP = 0xE000F010u; // STK_CMPLR
  inline constexpr std::uintptr_t CLINT_MTIMECMP_STRIDE = 0u;
  // No MSIP: the PFIC path never uses it (soft-irq.cpp compiles out); kept
  // so clint.hpp compiles.
  inline constexpr std::uintptr_t CLINT_MSIP = 0u;
  inline constexpr std::uintptr_t CLINT_MSIP_STRIDE = 0u;
  inline constexpr bool MTIMECMP_SPLIT32 = true;
  inline constexpr bool MTIME_READABLE = true;
  inline constexpr bool TIME_CSR = false;

  inline constexpr std::uint32_t TIMEBASE_HZ = 144000000u;
} // namespace riscv_board

extern "C"
{
  void riscv_board_initialize_early (void);
  void riscv_board_initialize (void);
  void riscv_board_console_write (const char* s, std::size_t n);
}
