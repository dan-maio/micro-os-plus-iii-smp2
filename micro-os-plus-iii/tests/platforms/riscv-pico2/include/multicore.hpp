/*
 * multicore.hpp -- start core 1 of the RP2350 (RISC-V).
 *
 * Core 1 waits in the boot ROM. launch_core1() resets it (PSM), then runs the
 * boot ROM's FIFO handshake {0, 0, 1, mtvec, sp, entry} -- the pico-sdk's
 * multicore_launch_core1_raw(), with the trap vector as the "vector table".
 * Core 1 then enters `entry` with no gp, so the board passes a trampoline
 * (startup: riscv_secondary_trampoline) that loads gp and the trap vector
 * and jumps to the real entry.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

#include <riscv-smp.hpp>

namespace multicore
{
  void
  launch_core1 (void (*entry) (void), std::uintptr_t stack_top);
} // namespace multicore
