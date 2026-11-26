/*
 * psram.hpp -- RP2350 QSPI PSRAM on the QMI's second chip select (CS1, XIP
 * window 1): on the WeAct RP2350B an 8 MB APS6404 with CS on GPIO0
 * (PICO2_PSRAM_CS_PIN). After psram::init() it is mapped read-write at
 * 0x11000000.
 *
 * With PICO2_PSRAM the board's riscv_board_initialize_memory() (startup.S
 * calls it before the .data copy) runs init(), and linker-psram.ld puts
 * .data, .bss and the heap there.
 *
 * Port of the Cortex-M pico2 board's glue/psram.cpp.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace psram
{
  inline constexpr std::uint32_t BASE = 0x11000000u;
  inline constexpr std::uint32_t SIZE = 8u * 1024u * 1024u;

  // Reset the PSRAM, enable quad mode, configure the QMI M1 read/write
  // formats and timing, make window 1 writable; true if a word written at
  // BASE reads back. Runs from SRAM (.psram_ram).
  bool init (void);

  // The device ID (SPI 0x9F) through QMI direct mode: out[0] = MFID
  // (APS6404: 0x0D), out[1] = KGD (0x5D), out[2..7] = EID. Resets the
  // device to SPI mode first, so call init() again afterwards.
  void read_id (std::uint8_t out[8]);
} // namespace psram
