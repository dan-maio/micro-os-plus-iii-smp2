/*
 * board.cpp -- Milk-V Duo start-up hooks for the RISC-V port.
 *
 * What runs before µOS++ on this board: boot ROM -> minimal fip (BL2: clocks,
 * DDR, UART4 + JTAG pinmux) -> OpenSBI + test payload, parked. OpenOCD
 * then halts the hart, loads the test into DDR and starts it at _start in
 * M-mode (hw.sh). So the clocks, DDR and the console are already up; the
 * board only has to make sure nothing OpenSBI left armed can fire.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>
#include <board.hpp>

namespace
{
  // PLIC: priority threshold of hart 0's M-mode context. 7 (the maximum,
  // riscv,max-priority = <0x07>) masks every source OpenSBI or the payload
  // may have enabled; the port does not use external interrupts yet.
  constexpr std::uintptr_t PLIC_M_THRESHOLD
      = riscv_board::PLIC_BASE + 0x200000u;
} // namespace

extern "C" void
riscv_board_initialize_early (void)
{
  *reinterpret_cast<volatile std::uint32_t*> (PLIC_M_THRESHOLD) = 7;
  RISCV_CSR_CLEAR (mie, riscv::csr::MIE_MEIE);
}

extern "C" void
riscv_board_initialize (void)
{
  uart::init (board::UART_BAUD);
}
