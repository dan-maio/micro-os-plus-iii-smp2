/*
 * sdio1.hpp -- the VisionFive 2 Lite's SDIO1 host (JH-7110S mmc1, a Synopsys
 * DesignWare MMC controller at 0x16020000) as an SDIO *card* host: the
 * on-board Wi-Fi module hangs on it.
 *
 * Sources:
 *   jh7110.dtsi (Linux)              mmc1: mmc@16020000, fifo-depth 32,
 *                                    clocks SDIO1_AHB (92) / SDIO1_SDCARD
 *                                    (94), reset SDIO1_AHB (65)
 *   jh7110s-starfive-visionfive-2-lite.dtsi (Linux, mainline)
 *                                    mmc1: 4-bit, non-removable, 50 MHz max,
 *                                    CLK GPIO10, CMD GPIO9, DATA0..3 GPIO11,
 *                                    12, 7, 8 (pull-up, 12 mA)
 *   jh7110s-starfive-visionfive-2-lite.dts (StarFive vendor tree, 6.12)
 *                                    gpio_wl_reg_on = <&sysgpio 33 ...>
 *   jh7110-pinfunc.h                 the GPOUT/GPOEN/GPI function numbers
 *   drivers/mmc/host/dw_mmc.{c,h}    registers, clock update, reset
 *   SDIO Simplified Specification 3.00: CMD5, CMD52, CMD53, CCCR, FBR, CIS
 *
 * Polled, like the board's eMMC driver (src/emmc.cpp): no interrupts, no
 * internal DMA; 512-byte blocks through the FIFO.
 *
 * NOT YET RUN ON THE BOARD: written from the sources above.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace sdio1
{
  inline constexpr std::uint32_t block_size = 512;

  struct info
  {
    std::uint32_t verid{ 0 };       // controller version (VERID)
    std::uint32_t clk_ahb{ 0 };     // SYSCRG SDIO1_AHB register after init
    std::uint32_t clk_card{ 0 };    // SYSCRG SDIO1_SDCARD register after init
    std::uint32_t ocr{ 0 };         // CMD5 response
    std::uint16_t rca{ 0 };
    std::uint8_t functions{ 0 };    // I/O functions the card reports
    std::uint8_t cccr_rev{ 0 };     // CCCR 0x00
    std::uint8_t card_caps{ 0 };    // CCCR 0x08
    std::uint16_t vendor{ 0 };      // CISTPL_MANFID, common CIS
    std::uint16_t device{ 0 };
    std::uint16_t f1_vendor{ 0 };   // CISTPL_MANFID, function 1 CIS (0 = none)
    std::uint16_t f1_device{ 0 };
    bool reg_on_high{ true };       // the WL_REG_ON level that woke the card
    std::uint32_t card_hz{ 0 };     // the data clock
  };

  /// Clocks, reset, pin mux, module power, card identification, 4-bit bus.
  bool
  init (info& out);

  /// CMD52: one register of function `fn`.
  bool
  read_byte (unsigned fn, std::uint32_t addr, std::uint8_t& value);

  bool
  write_byte (unsigned fn, std::uint32_t addr, std::uint8_t value);

  /// CMD53, fixed address (a FIFO register), block mode: `bytes` must be a
  /// multiple of block_size.  `buf` need not be aligned.
  bool
  read_fifo (unsigned fn, std::uint32_t addr, std::uint8_t* buf,
             std::uint32_t bytes);

  bool
  write_fifo (unsigned fn, std::uint32_t addr, const std::uint8_t* buf,
              std::uint32_t bytes);

  /// FBR block size of a function, and its I/O enable + ready wait.
  bool
  set_block_size (unsigned fn, std::uint16_t size);

  bool
  enable_function (unsigned fn);

  /// The last failure: the command and the raw interrupt status.
  const char*
  last_error (void);

  /// Busy-wait `us` microseconds on the CLINT (no scheduler involved).
  void
  delay_us (std::uint32_t us);
} // namespace sdio1
