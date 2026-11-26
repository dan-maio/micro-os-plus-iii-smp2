/*
 * ram-disk.hpp -- a 512 MiB RAM disk in the VisionFive 2 Lite's DDR, used as
 * FatFs drive 0 by vf2-wifi-ram-disk.
 *
 *   0x40000000  OpenSBI (left by the SPL; not used by µOS++)
 *   0x40200000  the test image, its stacks and heap (linker.ld: 32 MiB)
 *   0x42200000  ... unused ...
 *   0x60000000  the RAM disk, sector 0
 *   0x7fffffff  its last byte
 *
 * The board has at least 2 GiB of DDR (0x40000000..0xbfffffff), so the disk
 * is far from both the image and the top of DDR, where U-Boot relocated
 * itself before the test was loaded.  µOS++ runs in machine mode with the
 * MMU off: the disk is plain physical memory and a sector access is a
 * memcpy.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace ram_disk
{
  inline constexpr std::uintptr_t base = 0x6000'0000u;
  inline constexpr std::uint64_t size_bytes = 512ull * 1024 * 1024;
  inline constexpr std::uint32_t sector_size = 512;
  inline constexpr std::uint32_t sectors
      = static_cast<std::uint32_t> (size_bytes / sector_size);

  /// Check that the 512 MiB are real, distinct memory (write two patterns
  /// 512 MiB apart and read both back; the original words are restored).
  /// Idempotent.
  bool
  probe (void);

  [[nodiscard]] bool
  available (void);

  [[nodiscard]] const char*
  last_error (void);

  bool
  read (std::uint32_t sector, void* buffer, std::uint32_t count);

  bool
  write (std::uint32_t sector, const void* buffer, std::uint32_t count);
} // namespace ram_disk
