/*
 * board.cpp -- MangoPi MQ-Pro start-up hooks for the RISC-V port.
 *
 * What runs before µOS++: the D1 boot ROM (no bootable media -> FEL), xfel
 * (DDR controller, then `write` + `exec` at 0x40000000).  The clocks, DDR and
 * UART are not configured for us by a BL2: this board brings UART0 up itself
 * and sets the C906's MAEE bit so the port's T-Head page attributes work.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <blackbox.hpp>
#include <board.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

namespace
{
  constexpr std::uintptr_t RTC_GP_DATA2 = 0x07090108u;
  constexpr std::uintptr_t RTC_GP_DATA3 = 0x0709010Cu;
  constexpr std::uintptr_t RTC_GP_DATA4 = 0x07090110u;
  constexpr std::uintptr_t WDOG_SOFT_RST = 0x020500A8u;
  constexpr std::uintptr_t WDOG_CFG = 0x020500B4u;
  constexpr std::uintptr_t WDOG_MODE = 0x020500B8u;

  constexpr std::uint32_t EFEX_MAGIC = 0x5AA5A55Au;
  constexpr std::uint32_t WDOG_KEY = 0x16AAu << 16;

  // The xfel-only runner reads these after the board is back in FEL:
  //   GP_DATA3 = RESULT_MAGIC, GP_DATA4 = 0 PASS / 1 FAIL / 2 SKIP.
  // The RTC domain survives the watchdog reset; UART is left entirely to
  // the user and is never opened by hw.sh.
  constexpr std::uint32_t RESULT_MAGIC = 0x4D515052u; // 'MQPR'

  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }
} // namespace

extern "C" void
riscv_board_initialize_early (void)
{
  // T-Head C906 extended page attributes (mxstatus.MAEE, bit 21).  The port's
  // MMU map uses PTE bits 63..59 for cacheability; without MAEE the C906
  // ignores them and translated RAM pages are non-cacheable.
  __asm__ volatile("csrs 0x7c0, %0" ::"r"(1u << 21) : "memory");

  // I-cache and D-cache (C906 MHCR bits 0 and 1).  They are off after reset;
  // the single hart and the port's cache-aware code make this safe.
  __asm__ volatile("csrs 0x7c1, %0" ::"r"(3u) : "memory");

  // No external interrupts: the port uses the CLINT timer/software lines.
  // xfel/the BROM left the PLIC in an unknown state.
  RISCV_CSR_CLEAR (mie, riscv::csr::MIE_MEIE);
}

extern "C" void
riscv_board_initialize (void)
{
  blackbox::reset ();
  uart::init (board::UART_BAUD);
}

/*
 * Reset the SoC and ask the boot chain for FEL.  This is the hardware test
 * runner's "next test" button: Allwinner efex in RTC GP_DATA2, then the D1
 * watchdog soft-reset (the same value xfel reset writes).  With no SD card
 * the BROM falls back to FEL; a bootloader that honours efex does too.
 */
extern "C" [[noreturn]] void
riscv_board_reset_to_fel (void)
{
  reg (RTC_GP_DATA2) = EFEX_MAGIC;
  __asm__ volatile("fence rw, rw" ::: "memory");

  reg (WDOG_SOFT_RST) = WDOG_KEY | 0x1u;

  // Fallback if the soft-reset bit does not fire: 1 s watchdog reset.
  reg (WDOG_CFG) = WDOG_KEY | 0x1u;
  reg (WDOG_MODE) = WDOG_KEY | 0x11u;

  for (;;)
    {
      __asm__ volatile("wfi");
    }
}

/*
 * Every test end goes through here: PASS from hw_result, PASS/FAIL from
 * _Exit when os_main returns, FAIL from the trap handler.  The verdict is
 * written to the battery-backed RTC registers before the reset, so the
 * xfel-only runner can read it once the BROM is back in FEL.
 */
extern "C" [[noreturn]] void
riscv_board_test_finish (bool pass)
{
  reg (RTC_GP_DATA3) = RESULT_MAGIC;
  reg (RTC_GP_DATA4) = pass ? 0u : 1u;
  __asm__ volatile("fence rw, rw" ::: "memory");
  riscv_board_reset_to_fel ();
}

extern "C" [[noreturn]] void
riscv_board_exit (int code)
{
  riscv_board_test_finish (code == 0);
}
