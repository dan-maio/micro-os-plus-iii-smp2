/*
 * board.cpp -- StarFive VisionFive 2 Lite start-up hooks for the RISC-V port.
 *
 * What runs before µOS++: BootROM -> U-Boot SPL (clocks, DDR training) ->
 * OpenSBI -> U-Boot, stopped at its prompt. OpenOCD then halts the U74
 * harts, loads the test into DDR and starts at _start, in M-mode, hart
 * BOOT_HART -- and for an SMP image harts 2..4 too, which the port parks
 * until CPU 0 releases them (hw.sh). Harts an image does not drive stay
 * debug-halted. Clocks, DDR and UART0 are up; the board only makes sure
 * nothing OpenSBI/U-Boot left armed in the PLIC can fire.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>
#include <board.hpp>

namespace
{
  // PLIC (SiFive): enable bits at 0x2000 + 0x80 * ctx, threshold at
  // 0x200000 + 0x1000 * ctx. 136 sources -> 5 enable words.
  constexpr unsigned PLIC_ENABLE_WORDS = (136u + 1u + 31u) / 32u;

  inline volatile std::uint32_t&
  reg (std::uintptr_t addr)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (addr);
  }
} // namespace

// Where hw.sh parks the U74s it does not start (riscv_board_park): a jump to
// itself, nothing else. Its own section, kept by linker.ld, since nothing in
// the image calls it. Never wfi.
__asm__ (".section .text.park, \"ax\"\n"
         ".globl riscv_board_park\n"
         ".type riscv_board_park, @function\n"
         "riscv_board_park:\n"
         "  j riscv_board_park\n"
         ".size riscv_board_park, . - riscv_board_park\n"
         ".previous\n");

extern "C" void
riscv_board_initialize_early (void)
{
  // Every source off, threshold at the maximum (7), for the M-mode context
  // of every U74 -- the ones a build does not drive too: the port does not
  // use external interrupts here, and OpenSBI/U-Boot may have left some on.
  // Runs once, on CPU 0, before any other hart is released.
  for (unsigned h = riscv_board::U74_FIRST; h <= riscv_board::U74_LAST; ++h)
    {
      const unsigned ctx = riscv_board::plic_m_context (h);
      for (unsigned i = 0; i < PLIC_ENABLE_WORDS; ++i)
        {
          reg (riscv_board::PLIC_BASE + 0x2000u + 0x80u * ctx + 4u * i) = 0;
        }
      reg (riscv_board::PLIC_BASE + 0x200000u + 0x1000u * ctx) = 7;
    }
  RISCV_CSR_CLEAR (mie, riscv::csr::MIE_MEIE);
}

extern "C" void
riscv_board_initialize (void)
{
  uart::init (board::UART_BAUD);
}
