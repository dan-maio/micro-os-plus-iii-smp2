/*
 * riscv-board.hpp -- the facts the RISC-V port asks of a board.
 *
 * MangoPi MQ-Pro: Allwinner D1, one T-Head C906 (RV64GC), 512 MiB or 1 GiB
 * DDR3 at 0x40000000.  xfel brings DDR up and loads this image into DRAM.
 *
 * Sources:
 *   D1 device tree (Linux, sun20i-d1.dtsi / sunxi-d1s-t113.dtsi)
 *     timebase-frequency = <24000000>
 *     clint@14000000 { compatible = "allwinner,sun20i-d1-clint",
 *                      "thead,c900-clint"; reg = <0x14000000 0xc000>;
 *                      reg-io-width = <4>; }
 *     plic@10000000  { compatible = "allwinner,sun20i-d1-plic",
 *                      "thead,c900-plic"; reg = <0x10000000 0x4000000>; }
 *   C906 user manual, CLINT table: lower 27 bits are the standard CLINT
 *     layout (MSIP +0, MTIMECMPL0 +0x4000, STIMECMPL0 +0xD000); there is no
 *     MTIME register, so time comes from the `time` CSR (rdtime).
 *   Allwinner D1 user manual: CLINT/RTC/GPIO/CCU/UART windows.
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
  // The D1 has one C906, mhartid 0.
  inline constexpr unsigned BOOT_HART = 0;
  static_assert (RISCV_HART_BASE == 0u, "the D1 C906 is mhartid 0");

  inline constexpr std::uintptr_t CLINT_BASE = 0x14000000u;
  inline constexpr std::uintptr_t CLINT_MSIP = CLINT_BASE + 0x0000u;
  inline constexpr std::uintptr_t CLINT_MTIMECMP = CLINT_BASE + 0x4000u;
  inline constexpr std::uintptr_t CLINT_MTIME = CLINT_BASE + 0xBFF8u;
  // A standard CLINT register stride; the D1's bus is 32-bit only.
  inline constexpr std::uintptr_t CLINT_MTIMECMP_STRIDE = 8u;
  inline constexpr std::uintptr_t CLINT_MSIP_STRIDE = 4u;
  inline constexpr bool MTIMECMP_SPLIT32 = true;
  // No MTIME register on the C906: rdtime reads the same counter.
  inline constexpr bool MTIME_READABLE = false;
  inline constexpr bool TIME_CSR = true;

  inline constexpr std::uint32_t TIMEBASE_HZ = 24000000u;

  inline constexpr std::uintptr_t PLIC_BASE = 0x10000000u;

  // C906 extended page attributes.  The board's riscv_board_initialize_early()
  // sets mxstatus.MAEE = 1 before mmu::init(), so these are honoured.  RAM =
  // Cacheable|Bufferable|Shareable; I/O = Strong-order|Shareable (the values
  // the Allwinner SDK's Linux uses).
  inline constexpr std::uint64_t PTE_ATTR_RAM
      = mmu::PTE_THEAD_C | mmu::PTE_THEAD_B | mmu::PTE_THEAD_SH;
  inline constexpr std::uint64_t PTE_ATTR_DEV
      = mmu::PTE_THEAD_SO | mmu::PTE_THEAD_SH;
  inline constexpr std::uint64_t FLAGS_RAM = mmu::FLAGS_RAM | PTE_ATTR_RAM;
  inline constexpr std::uint64_t FLAGS_DEV = mmu::FLAGS_DEV | PTE_ATTR_DEV;

  // Sv39 identity map (2 MiB leaves).  The windows are the D1 user manual's
  // memory map, rounded out to 2 MiB.  The heap/image stay in the first
  // 512 MiB, which both the 512 MiB and 1 GiB boards have.
  inline constexpr mmu::region MMU_REGIONS[] = {
    { 0x00000000u, 0x00200000u, FLAGS_DEV }, // BROM + SRAM A1
    { 0x02000000u, 0x01000000u, FLAGS_DEV }, // GPIO, CCU, UART0, TIMER
    { 0x04000000u, 0x00200000u, FLAGS_DEV }, // SDC0 (card), SDC1 (SDIO Wi-Fi), USB0
    { 0x07000000u, 0x00200000u, FLAGS_DEV }, // RTC (efex), CPUX
    { 0x10000000u, 0x04000000u, FLAGS_DEV }, // PLIC
    { 0x14000000u, 0x00200000u, FLAGS_DEV }, // CLINT
    { 0x40000000u, 0x20000000u, FLAGS_RAM }, // DDR, first 512 MiB
  };
} // namespace riscv_board

extern "C"
{
  // Called by the port's start-up (src/startup.cpp).
  void riscv_board_initialize_early (void);
  void riscv_board_initialize (void);
  // The raw console: trace, printf and the fault report all end here.
  void riscv_board_console_write (const char* s, std::size_t n);

  // End the run and return the SoC to FEL (Allwinner efex + watchdog).
  // Used by test/<board>/include/hw_result.hpp on hardware.
  [[noreturn]] void riscv_board_reset_to_fel (void);

  // Record PASS/FAIL where the xfel-only runner can read it after the reset
  // (RTC GP_DATA3/4), then reset to FEL.  Every end path uses this: the
  // tests' hw_result, a returning os_main (_Exit), and a trap.
  [[noreturn]] void riscv_board_test_finish (bool pass);

  // The port's weak _Exit/trap hook (src/semihosting-exit.cpp, src/trap.cpp).
  [[noreturn]] void riscv_board_exit (int code);
}
