/*
 * emmc-area.hpp -- find the eMMC space a test may write without touching
 * Linux (test/visionfive2-lite: emmc-probe, emmc-fs-test).
 *
 * Measured 2026-11-07: a Samsung "A31M8C", 122142720 sectors (58.24 GiB),
 * GPT with four partitions -- u-boot-spl, u-boot, EFI System (vfat, the
 * boot partition) and the Linux root, which ends at LBA 109928447
 * (52.42 GiB). So the space from 40 GiB is NOT free; the area is the space
 * AFTER THE HIGHEST PARTITION:
 *
 *   start  the sector after the highest partition, rounded up to 1 MiB
 *   end    the GPT's last usable sector + 1 (the backup entries and header
 *          follow it), minus a 1 MiB margin, rounded down to 1 MiB
 *
 * find_area() reads the protective MBR, the GPT header and the entries, and
 * refuses (returns false) without a valid GPT, with an unexpected entry
 * layout, or when less than 64 MiB is left. The partition table itself is
 * never written.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <emmc.hpp>

#include <cstdint>
#include <cstring>

namespace emmc_area
{
  constexpr std::uint64_t MiB_SECTORS = 2048; // 1 MiB of 512-byte sectors

  struct partition
  {
    std::uint64_t first;
    std::uint64_t last;
    std::uint8_t type[16];
    char name[37];
  };

  struct area
  {
    std::uint64_t card_sectors;
    std::uint64_t gpt_backup_lba;
    std::uint64_t last_usable;
    std::uint32_t n_partitions;
    partition parts[8];
    std::uint64_t start; // first sector of the area
    std::uint64_t end;   // one past its last sector
  };

  inline std::uint64_t
  le64 (const std::uint8_t* p)
  {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
      {
        v = (v << 8) | p[i];
      }
    return v;
  }

  inline std::uint32_t
  le32 (const std::uint8_t* p)
  {
    return static_cast<std::uint32_t> (p[0]) | (p[1] << 8) | (p[2] << 16)
           | (static_cast<std::uint32_t> (p[3]) << 24);
  }

  // `why` gets a short reason on failure.
  inline bool
  find_area (const emmc::info& card, area& a, const char*& why)
  {
    static std::uint8_t sector[512];
    static std::uint8_t entries[128 * 128];
    std::memset (&a, 0, sizeof (a));
    a.card_sectors = card.sectors;

    if (emmc::read (0, sector, 1) != emmc::status::ok)
      {
        why = "read LBA 0";
        return false;
      }
    if (sector[510] != 0x55 || sector[511] != 0xAA || sector[446 + 4] != 0xEE)
      {
        why = "LBA 0 is not a protective MBR";
        return false;
      }
    if (emmc::read (1, sector, 1) != emmc::status::ok)
      {
        why = "read LBA 1";
        return false;
      }
    if (std::memcmp (sector, "EFI PART", 8) != 0)
      {
        why = "no GPT header at LBA 1";
        return false;
      }
    a.gpt_backup_lba = le64 (sector + 32);
    a.last_usable = le64 (sector + 48);
    const std::uint64_t entries_lba = le64 (sector + 72);
    const std::uint32_t n = le32 (sector + 80);
    const std::uint32_t size = le32 (sector + 84);
    if (size != 128 || n == 0 || n > 128 || a.last_usable >= a.gpt_backup_lba
        || a.gpt_backup_lba >= card.sectors)
      {
        why = "unexpected GPT layout";
        return false;
      }
    if (emmc::read (entries_lba, entries, (n * size + 511) / 512)
        != emmc::status::ok)
      {
        why = "read GPT entries";
        return false;
      }

    std::uint64_t highest = 0;
    for (std::uint32_t i = 0; i < n; ++i)
      {
        const std::uint8_t* e = entries + i * size;
        bool used = false;
        for (unsigned k = 0; k < 16; ++k)
          {
            used = used || (e[k] != 0);
          }
        if (!used)
          {
            continue;
          }
        if (a.n_partitions == sizeof (a.parts) / sizeof (a.parts[0]))
          {
            why = "more partitions than expected";
            return false;
          }
        partition& p = a.parts[a.n_partitions++];
        p.first = le64 (e + 32);
        p.last = le64 (e + 40);
        std::memcpy (p.type, e, 16);
        unsigned k = 0;
        for (; k < 36; ++k)
          {
            const std::uint8_t c = e[56 + 2 * k];
            if (c == 0)
              {
                break;
              }
            p.name[k] = (c >= 0x20 && c < 0x7F) ? static_cast<char> (c) : '?';
          }
        p.name[k] = '\0';
        if (p.last > highest)
          {
            highest = p.last;
          }
      }

    a.start = (highest + 1 + MiB_SECTORS - 1) & ~(MiB_SECTORS - 1);
    const std::uint64_t limit = a.last_usable + 1; // < backup GPT
    if (limit < MiB_SECTORS + a.start)
      {
        why = "no space after the last partition";
        return false;
      }
    a.end = (limit - MiB_SECTORS) & ~(MiB_SECTORS - 1);
    if (a.end <= a.start || a.end - a.start < 64 * MiB_SECTORS)
      {
        why = "less than 64 MiB after the last partition";
        return false;
      }
    // Paranoia: no partition may reach into the area.
    for (std::uint32_t i = 0; i < a.n_partitions; ++i)
      {
        if (!(a.parts[i].last < a.start || a.parts[i].first >= a.end))
          {
            why = "a partition overlaps the area";
            return false;
          }
      }
    return true;
  }

  // FNV-1a over `count` sectors from `lba`, read in 64-sector chunks.
  inline bool
  hash_sectors (std::uint64_t lba, std::uint64_t count, std::uint64_t& h)
  {
    static std::uint8_t buf[64 * 512];
    while (count != 0)
      {
        const std::uint32_t n
            = static_cast<std::uint32_t> (count > 64 ? 64 : count);
        if (emmc::read (lba, buf, n) != emmc::status::ok)
          {
            return false;
          }
        for (std::uint32_t i = 0; i < n * 512; ++i)
          {
            h ^= buf[i];
            h *= 0x100000001b3ull;
          }
        lba += n;
        count -= n;
      }
    return true;
  }
} // namespace emmc_area
