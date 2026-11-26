/*
 * irq.cpp -- the CV1800B PLIC, nested (include/duo-irq.hpp).
 *
 * PLIC layout (RISC-V PLIC spec; C906 PLIC, hart 0 M-mode = context 0):
 *   priority[n]   base + 4 n
 *   enable        base + 0x2000 (context 0), one bit per source
 *   threshold     base + 0x200000
 *   claim/complete base + 0x200004
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <duo-irq.hpp>
#include <riscv-board.hpp>

#include <cstdint>

namespace
{
  constexpr std::uintptr_t PLIC = riscv_board::PLIC_BASE;
  constexpr std::uintptr_t PRIORITY = PLIC;
  constexpr std::uintptr_t ENABLE = PLIC + 0x2000u;
  constexpr std::uintptr_t THRESHOLD = PLIC + 0x200000u;
  constexpr std::uintptr_t CLAIM = PLIC + 0x200004u;

  constexpr std::uintptr_t MIE_MSIE = 1u << 3, MIE_MTIE = 1u << 7,
                           MIE_MEIE = 1u << 11;

  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  // Load a register for its side effect (an EOI, a status clear, a write
  // landing). Not `(void) reg (a)`: in C++ that is no access to a volatile
  // (GCC: "conversion to void will not access object") -- a missing EOI read
  // left Timer1's interrupt asserted for good (measured: an interrupt storm).
  inline void
  touch (std::uintptr_t a)
  {
    const std::uint32_t v = *reinterpret_cast<volatile std::uint32_t*> (a);
    __asm__ volatile("" ::"r"(v));
  }

  duo_irq::handler_t g_handlers[duo_irq::COUNT];
  volatile unsigned g_depth = 0;
} // namespace

namespace duo_irq
{
  volatile unsigned nested_entries = 0;

  void
  init (void)
  {
    for (unsigned w = 0; w < COUNT / 32; ++w)
      {
        reg (ENABLE + 4u * w) = 0;
      }
    reg (THRESHOLD) = 0;
    touch (THRESHOLD);
    __asm__ volatile("csrs mie, %0" ::"r"(MIE_MEIE) : "memory");
  }

  void
  set_handler (unsigned irq, handler_t h)
  {
    if (irq < COUNT)
      {
        g_handlers[irq] = h;
      }
  }

  void
  set_priority (unsigned irq, unsigned prio)
  {
    if (irq < COUNT)
      {
        reg (PRIORITY + 4u * irq) = (prio > 7u) ? 7u : prio;
      }
  }

  void
  enable (unsigned irq)
  {
    if (irq < COUNT)
      {
        reg (ENABLE + 4u * (irq / 32u)) |= 1u << (irq % 32u);
      }
  }

  void
  disable (unsigned irq)
  {
    if (irq < COUNT)
      {
        reg (ENABLE + 4u * (irq / 32u)) &= ~(1u << (irq % 32u));
      }
  }
} // namespace duo_irq

// The trap's machine external interrupt (trap.cpp), MIE clear on entry.
extern "C" void
riscv_board_external_interrupt (void)
{
  for (;;)
    {
      const std::uint32_t id = reg (CLAIM);
      if (id == 0)
        {
          break;
        }
      const duo_irq::handler_t h = (id < duo_irq::COUNT) ? g_handlers[id] : nullptr;
      if (h == nullptr)
        {
          duo_irq::disable (id);
          reg (CLAIM) = id; // complete
          continue;
        }

      // Only higher priorities may come in now; the tick and the soft IRQ
      // stay out until this handler is done (CLEARTS on Hazard3).
      const std::uint32_t thr = reg (THRESHOLD);
      reg (THRESHOLD) = reg (PRIORITY + 4u * id);
      touch (THRESHOLD); // the write has landed before MIE opens
      std::uintptr_t mie_saved;
      __asm__ volatile("csrrc %0, mie, %1"
                       : "=r"(mie_saved)
                       : "r"(MIE_MTIE | MIE_MSIE)
                       : "memory");
      if (g_depth != 0)
        {
          duo_irq::nested_entries = duo_irq::nested_entries + 1;
        }
      g_depth = g_depth + 1;

      __asm__ volatile("csrsi mstatus, 8" ::: "memory");
      h ();
      __asm__ volatile("csrci mstatus, 8" ::: "memory");

      g_depth = g_depth - 1;
      __asm__ volatile("csrs mie, %0" ::"r"(mie_saved & (MIE_MTIE | MIE_MSIE))
                       : "memory");
      reg (CLAIM) = id; // complete
      reg (THRESHOLD) = thr;
      touch (THRESHOLD);
    }
}
