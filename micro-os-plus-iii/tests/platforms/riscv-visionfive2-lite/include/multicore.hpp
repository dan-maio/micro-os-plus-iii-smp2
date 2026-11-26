/*
 * multicore.hpp -- start the other U74 harts, VisionFive 2 Lite.
 *
 * hw.sh starts every U74 at _start; the port parks all but CPU 0 (startup.S
 * riscv_secondary_park). launch_core(n, ...) releases CPU n (mhartid n + 1)
 * into `entry` on `stack_top`, as QEMU virt's launch_core1() releases hart 1.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

#include <riscv-smp.hpp>

namespace multicore
{
  inline void
  launch_core (unsigned cpu, void (*entry) (void), std::uintptr_t stack_top)
  {
    riscv_smp_release_hart (cpu, entry, stack_top);
  }

  inline void
  launch_core1 (void (*entry) (void), std::uintptr_t stack_top)
  {
    launch_core (1, entry, stack_top);
  }
} // namespace multicore
