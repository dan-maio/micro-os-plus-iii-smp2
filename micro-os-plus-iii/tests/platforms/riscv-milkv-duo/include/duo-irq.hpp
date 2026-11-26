/*
 * duo-irq.hpp -- the CV1800B PLIC for the C906 hart's machine mode, with
 * nested interrupts (src/irq.cpp).
 *
 * Numbers are the SoC's interrupt IDs for the C906 ("master") core: SG2002
 * TRM table 3.2, the same family; UART4 = 48 agrees with the CV1800B device
 * tree (cv180x_base_riscv.dtsi).
 *
 * Priorities are the PLIC's, 1 (lowest) .. 7 (highest); 0 never interrupts.
 * The dispatcher (riscv_board_external_interrupt) claims an interrupt,
 * raises the PLIC threshold to its priority, and runs its handler with
 * mstatus.MIE set, so a higher-priority interrupt preempts it. While a
 * handler runs, mie.MTIE/MSIE are cleared, as Hazard3's meicontext.CLEARTS
 * does on the Pico 2: the tick and the soft-IRQ lines stay below every
 * external interrupt.
 *
 * Not the NVIC_* names: on this board those are the port's software IRQ
 * lines (soft-irq.cpp), which the CMSIS validator uses.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace duo_irq
{
  inline constexpr unsigned UART4 = 48;
  inline constexpr unsigned TIMER0 = 79; // ... TIMER7 = 86
  inline constexpr unsigned COUNT = 128;

  using handler_t = void (*) (void);

  // M-mode context of hart 0: every source disabled, threshold 0,
  // mie.MEIE set. Priorities are left as they are (0 after reset).
  void
  init (void);

  void
  set_handler (unsigned irq, handler_t h);
  void
  set_priority (unsigned irq, unsigned prio);
  void
  enable (unsigned irq);
  void
  disable (unsigned irq);

  // How many handler entries found another handler already running on this
  // hart (nesting depth > 1), for the tests.
  extern volatile unsigned nested_entries;
} // namespace duo_irq
