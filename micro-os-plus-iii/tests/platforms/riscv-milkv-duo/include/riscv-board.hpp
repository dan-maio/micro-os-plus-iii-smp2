/*
 * riscv-board.hpp -- the facts the RISC-V port asks of a board.
 *
 * Milk-V Duo (64 MB): Sophgo CV1800B, main hart = T-Head C906 @ 850 MHz,
 * 64 MiB DDR2 at 0x80000000.
 *
 * Every number below is taken from the Duo SDK, not from memory:
 *   duo-buildroot-sdk build/boards/default/dts/cv180x_riscv/cv180x_base_riscv.dtsi
 *     timebase-frequency = <25000000>
 *     clint@74000000 { compatible = "riscv,clint0"; clint,has-no-64bit-mmio; }
 *     plic0: interrupt-controller@70000000 { reg = <0x70000000 0x4000000>; }
 *   opensbi lib/utils/timer/fdt_timer_clint.c
 *     { .addr = 0x74000000, .has_64bit_mmio = 0 }  -> mtimecmp as two
 *     32-bit halves
 *   measured on the board (2026-11-05): a load from mtime (+0xBFF8) is a
 *     load access fault (mcause 5) -- even a 32-bit one. The cv1800 Eclipse
 *     project had found the same; OpenSBI's generic driver would read it,
 *     but the C906 does not implement it. Time comes from rdtime.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <mmu.hpp>

namespace riscv_board
{
  inline constexpr std::uintptr_t CLINT_BASE = 0x74000000u;
  inline constexpr std::uintptr_t CLINT_MSIP = CLINT_BASE + 0x0000u;
  inline constexpr std::uintptr_t CLINT_MTIMECMP = CLINT_BASE + 0x4000u;
  inline constexpr std::uintptr_t CLINT_MTIME = CLINT_BASE + 0xBFF8u;
  // Per-hart register strides (a standard CLINT).
  inline constexpr std::uintptr_t CLINT_MTIMECMP_STRIDE = 8u;
  inline constexpr std::uintptr_t CLINT_MSIP_STRIDE = 4u;
  // clint,has-no-64bit-mmio
  inline constexpr bool MTIMECMP_SPLIT32 = true;
  // mtime is not implemented on the C906: reading it faults (see above).
  inline constexpr bool MTIME_READABLE = false;

  // The port's clock source: the time CSR (rdtime) where the hart has it,
  // else the platform timer's MMIO mtime. Here: the C906 implements the time CSR (its CLINT mtime is not readable).
  inline constexpr bool TIME_CSR = true;

  inline constexpr std::uint32_t TIMEBASE_HZ = 25000000u;

  inline constexpr std::uintptr_t PLIC_BASE = 0x70000000u;

  // C906 extended PTE attributes (mxstatus.MAEE = 1, set by BL2; mmu.hpp).
  inline constexpr std::uint64_t PTE_ATTR_RAM
      = mmu::PTE_THEAD_C | mmu::PTE_THEAD_B | mmu::PTE_THEAD_SH;
  inline constexpr std::uint64_t PTE_ATTR_DEV
      = mmu::PTE_THEAD_SO | mmu::PTE_THEAD_SH;
  inline constexpr std::uint64_t FLAGS_RAM = mmu::FLAGS_RAM | PTE_ATTR_RAM;
  inline constexpr std::uint64_t FLAGS_DEV = mmu::FLAGS_DEV | PTE_ATTR_DEV;

  // Sv39 identity map (mmu.hpp). The MMIO windows are those of the cv1800
  // Eclipse project's mmu.c; DRAM is the Duo's 64 MiB.
  inline constexpr mmu::region MMU_REGIONS[] = {
    { 0x03000000u, 0x00200000u, FLAGS_DEV }, // TOP: pinmux, clk, rst, GPIO
    { 0x04000000u, 0x01000000u, FLAGS_DEV }, // HSPERI: UART, SD, USB
    { 0x05000000u, 0x00400000u, FLAGS_DEV }, // RTC domain
    { 0x0E000000u, 0x00200000u, FLAGS_DEV }, // AXI SRAM
    { 0x70000000u, 0x00400000u, FLAGS_DEV }, // PLIC
    { 0x74000000u, 0x00200000u, FLAGS_DEV }, // CLINT
    { 0x80000000u, 0x04000000u, FLAGS_RAM }, // DDR2, 64 MiB
  };
} // namespace riscv_board

extern "C"
{
  // Called by the port's start-up (src/startup.cpp).
  void riscv_board_initialize_early (void);
  void riscv_board_initialize (void);
  // The raw console: trace, printf and the fault report all end here.
  void riscv_board_console_write (const char* s, std::size_t n);
}
