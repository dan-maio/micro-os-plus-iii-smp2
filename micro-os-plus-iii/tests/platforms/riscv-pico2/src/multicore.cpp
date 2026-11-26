/*
 * multicore.cpp -- launch core 1 of the RP2350 (RISC-V) through the boot
 * ROM's FIFO handshake.
 *
 * As the pico-sdk's multicore_launch_core1_raw() does it: send
 * {0, 0, 1, vector, sp, entry}, each word echoed back by the
 * boot ROM, restarting on a mismatch; a 0 is preceded by a drain and an
 * h3.unblock (the Arm SEV). On RISC-V the "vector" is core 1's mtvec.
 *
 * Core 1 enters with no gp. It enters riscv_secondary_trampoline
 * (startup.S), which loads gp and jumps to riscv_secondary_c_entry -- the
 * entry the caller asked for.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <multicore.hpp>
#include <rp2350.hpp>

using rp2350::reg;

extern "C"
{
  void riscv_secondary_trampoline (void);
  void riscv_trap_entry (void);
  void (*volatile riscv_secondary_c_entry) (void) = nullptr;
}

namespace
{
  void
  fifo_drain (void)
  {
    while (reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD)
      {
        volatile std::uint32_t sink = reg (rp2350::SIO_FIFO_RD);
        (void) sink;
      }
  }

  void
  fifo_push (std::uint32_t v)
  {
    while (!(reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_RDY))
      {
      }
    reg (rp2350::SIO_FIFO_WR) = v;
    rp2350::h3_unblock ();
  }

  std::uint32_t
  fifo_pop (void)
  {
    while (!(reg (rp2350::SIO_FIFO_ST) & rp2350::FIFO_ST_VLD))
      {
      }
    return reg (rp2350::SIO_FIFO_RD);
  }
} // namespace

namespace multicore
{
  void
  launch_core1 (void (*entry) (void), std::uintptr_t stack_top)
  {
    riscv_secondary_c_entry = entry;
    __asm__ volatile("fence rw, rw" ::: "memory");

    // No PSM reset of core 1 here: after a chip reset it already waits in
    // the boot ROM's launch loop, which is what the pico-sdk's
    // multicore_launch_core1() relies on too (multicore_reset_core1() is a
    // separate call). Measured: forcing it off and on under the debugger,
    // OpenOCD reports "[rp2350.rv1] Hart unexpectedly reset!" and core 1
    // never answers the handshake.

    const std::uint32_t seq[6] = {
      0,
      0,
      1,
      static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (riscv_trap_entry)),
      static_cast<std::uint32_t> (stack_top & ~static_cast<std::uintptr_t> (15)),
      static_cast<std::uint32_t> (
          reinterpret_cast<std::uintptr_t> (riscv_secondary_trampoline)),
    };
    unsigned i = 0;
    while (i < 6)
      {
        const std::uint32_t cmd = seq[i];
        if (cmd == 0)
          {
            fifo_drain ();
            rp2350::h3_unblock ();
          }
        fifo_push (cmd);
        const std::uint32_t rsp = fifo_pop ();
        i = (rsp == cmd) ? i + 1 : 0;
      }
  }
} // namespace multicore
