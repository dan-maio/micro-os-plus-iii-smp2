/*
 * emmc.hpp -- the VisionFive 2 Lite's eMMC (JH-7110 SDIO0, a Synopsys
 * DesignWare MMC host at 0x16010000), polled, one block = 512 bytes.
 *
 * Sources (mainline Linux): jh7110.dtsi mmc0 (reg 0x16010000, fifo-depth 32,
 * data-addr 0 -> the FIFO offset follows VERID), jh7110-common.dtsi &mmc0
 * (ciu clock 50 MHz requested by Linux, 8-bit bus), the eMMC variant of the
 * Lite dts; drivers/mmc/host/dw_mmc.h/.c (register map, the clock-update
 * command, the controller reset).
 *
 * Measured on the board at the U-Boot prompt (2026-11-07): VERID 0x290a
 * (FIFO at 0x200), HCON H_DATA_WIDTH 1 = a 32-bit FIFO (dw_mci_probe(): 0
 * = 16, 1 = 32, 2 = 64 bits; read 64 bits at a time it gave garbage),
 * PWREN/CLKENA/CLKDIV/CTYPE 0 (U-Boot left the controller unused),
 * sdio0_sdcard enabled, PLL2 1188 MHz / 3 / 2 = 198 MHz controller clock.
 *
 * The card is run at 1-bit, legacy timing: 399 kHz while it is identified
 * (CLKDIV 248), then 24.75 MHz (CLKDIV 4; the legacy limit is 26 MHz).
 * Sector addressing (a card above 2 GB): the argument is the LBA.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace emmc
{
  enum class status
  {
    ok,
    timeout,     // no response / data in time
    crc,         // response or data CRC error
    error,       // any other controller error
    not_ready,   // the card never left the busy state of CMD1
    no_init      // init() was not called or failed
  };

  struct info
  {
    std::uint32_t verid;         // controller version
    std::uint32_t ocr;           // CMD1 response
    std::uint32_t cid[4];        // [0] = bits 127..96
    std::uint32_t csd[4];        // [0] = bits 127..96
    std::uint64_t sectors;       // EXT_CSD SEC_COUNT (512-byte blocks)
    std::uint8_t ext_csd_rev;    // EXT_CSD[192]
    char product[7];             // CID PNM
    std::uint8_t manufacturer;   // CID MID
  };

  // Reset the controller, identify the card, read its EXT_CSD.
  status
  init (info& out);

  // Read / write `count` 512-byte blocks at `lba`. `buf` need not be aligned.
  status
  read (std::uint64_t lba, void* buf, std::uint32_t count);

  status
  write (std::uint64_t lba, const void* buf, std::uint32_t count);

  // A short description of the last failure (the command and the raw
  // interrupt status), for the test's report.
  const char*
  last_error (void);
} // namespace emmc
