/*
 * riscv-board.hpp -- the facts the RISC-V port asks of a board.
 *
 * StarFive VisionFive 2 Lite: JH-7110S, U74-MC complex = 1 S7 (mhartid 0)
 * + 4 U74 (mhartid 1..4). µOS++ runs on the U74 with mhartid BOOT_HART.
 *
 * Sources:
 *   jh7110.dtsi (Linux)
 *     clint: timer@2000000 { reg = <0x0 0x2000000 0x0 0x10000>; }
 *     plic:  interrupt-controller@c000000 { reg = <0x0 0xc000000 0x0
 *            0x4000000>; riscv,ndev = <136>; interrupts-extended = <&cpu0_intc
 *            11>, <&cpu1_intc 11>, <&cpu1_intc 9>, ... }
 *   jh7110-common.dtsi: timebase-frequency = <4000000>
 *   SiFive U7 Core Complex manual (vic_u7_manual), CLINT memory map:
 *     0x0200_0000 + 4n  msip for hart n
 *     0x0200_4000 + 8n  mtimecmp for hart n (64-bit RW)
 *     0x0200_BFF8       mtime (64-bit RW)
 *   The U7 manual lists no `time` CSR among the hart's CSRs, so the port's
 *   clock is mtime over MMIO (TIME_CSR = false). To be confirmed on the
 *   board (clock-test), as the C906's unreadable mtime was.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include <mmu.hpp>
#include <port_ctx.hpp>

namespace riscv_board
{
  // µOS++'s CPU 0 is mhartid RISCV_HART_BASE (= 1, board.cmake; the port's
  // include/port_ctx.hpp): CPU n is the U74 with mhartid n + 1. The port
  // indexes the CLINT's per-hart registers by CPU, so they start at hart
  // BOOT_HART's slot below.
  inline constexpr unsigned BOOT_HART = RISCV_HART_BASE;
  static_assert (BOOT_HART == 1u, "the U74s are mhartid 1..4");

  inline constexpr std::uintptr_t CLINT_BASE = 0x02000000u;
  inline constexpr std::uintptr_t CLINT_MSIP_STRIDE = 4u;
  inline constexpr std::uintptr_t CLINT_MTIMECMP_STRIDE = 8u;
  inline constexpr std::uintptr_t CLINT_MSIP
      = CLINT_BASE + 0x0000u + BOOT_HART * CLINT_MSIP_STRIDE;
  inline constexpr std::uintptr_t CLINT_MTIMECMP
      = CLINT_BASE + 0x4000u + BOOT_HART * CLINT_MTIMECMP_STRIDE;
  inline constexpr std::uintptr_t CLINT_MTIME = CLINT_BASE + 0xBFF8u;
  // A standard SiFive CLINT: 64-bit mtimecmp/mtime registers.
  inline constexpr bool MTIMECMP_SPLIT32 = false;
  inline constexpr bool MTIME_READABLE = true;

  // No time CSR on the U74 (see above): the clock is the CLINT's mtime.
  inline constexpr bool TIME_CSR = false;

  inline constexpr std::uint32_t TIMEBASE_HZ = 4000000u;

  inline constexpr std::uintptr_t PLIC_BASE = 0x0C000000u;
  // PLIC contexts, in interrupts-extended order: 0 = hart 0 M, then M and S
  // for each U74 hart -> hart h's M-mode context is 2h - 1.
  constexpr unsigned
  plic_m_context (unsigned mhartid)
  {
    return 2u * mhartid - 1u;
  }
  inline constexpr unsigned PLIC_M_CONTEXT = plic_m_context (BOOT_HART);
  inline constexpr unsigned U74_FIRST = 1u, U74_LAST = 4u;

  // A standard Sv39 hart: no vendor PTE attributes.
  inline constexpr std::uint64_t FLAGS_RAM = mmu::FLAGS_RAM;
  inline constexpr std::uint64_t FLAGS_DEV = mmu::FLAGS_DEV;

  // Sv39 identity map (mmu.hpp), 2 MiB windows around the devices this board
  // drives (jh7110.dtsi), and the first GiB of DDR (every VisionFive 2 Lite
  // has at least 2 GiB; the image is at 0x40200000).
  inline constexpr mmu::region MMU_REGIONS[] = {
    { 0x02000000u, 0x00200000u, FLAGS_DEV }, // CLINT
    { 0x0C000000u, 0x00400000u, FLAGS_DEV }, // PLIC (priorities .. contexts)
    { 0x10000000u, 0x00200000u, FLAGS_DEV }, // UART0..2
    { 0x13000000u, 0x00200000u, FLAGS_DEV }, // sys_syscon, sysgpio (iomux)
    { 0x17000000u, 0x00200000u, FLAGS_DEV }, // aon_syscon, aongpio
    { 0x40000000u, 0x40000000u, FLAGS_RAM }, // DDR, first 1 GiB
  };
} // namespace riscv_board

extern "C"
{
  void riscv_board_initialize_early (void);
  void riscv_board_initialize (void);
  void riscv_board_console_write (const char* s, std::size_t n);
}
