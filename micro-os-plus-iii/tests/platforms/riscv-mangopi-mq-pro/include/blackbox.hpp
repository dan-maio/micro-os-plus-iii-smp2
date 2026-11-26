/*
 * blackbox.hpp -- the console log, kept in RAM so it can be read back.
 *
 * The MangoPi MQ-Pro setup has no debugger and UART0 belongs to the user: a
 * test that hangs or fails gives the runner nothing but a verdict.  So every
 * character written to the console is also appended to a small buffer in the
 * board's scratch area (above the image, never used by the kernel), and
 * hw.sh reads it back over xfel after the test has reset the SoC.
 *
 * Layout at board::SCRATCH_BASE:
 *
 *   0x00  magic   'BBLG'
 *   0x04  length  bytes of text written
 *   0x08  lost    bytes discarded after the buffer filled
 *   0x0c  reserved
 *   0x10  text
 *
 * DDR contents are not guaranteed across a reset, but xfel's `ddr d1`
 * initialises the controller without clearing the array, so in practice the
 * text is still there; hw.sh checks the magic and says so if it is not.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace blackbox
{
  inline constexpr std::uint32_t magic = 0x42424C47u; // 'BBLG'
  inline constexpr std::size_t capacity = 64u * 1024u;

  /// Forget the previous run. Called from riscv_board_initialize().
  void
  reset (void);

  /// Append one character. Safe to call from any thread; the tail is a
  /// best-effort log, not a channel with a contract.
  void
  put (char c);

  /// Bytes currently held (the text, not the header).
  std::uint32_t
  length (void);
} // namespace blackbox
