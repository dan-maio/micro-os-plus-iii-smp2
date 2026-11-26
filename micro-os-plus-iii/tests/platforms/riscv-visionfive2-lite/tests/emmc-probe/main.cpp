/*
 * emmc-probe (VisionFive 2 Lite) -- READ ONLY: identify the eMMC and list its
 * partitions, and find whether the space from 40 GiB on is free.
 *
 * Nothing is written to the card: only CMD0/1/2/3/9/7 (identification),
 * CMD8 (EXT_CSD) and CMD17/18 (reads). It reads the protective MBR (LBA 0),
 * the GPT header (LBA 1) and the partition entries, prints every partition,
 * and reports whether the space from 40 GiB is free (measured 2026-11-07:
 * it is not, the Linux root runs to 52.42 GiB) and the area emmc-fs-test
 * uses instead (emmc-area.hpp: after the highest partition, before the
 * backup GPT).
 *
 * PASS when the card was read, the GPT is valid and emmc_area::find_area()
 * found an area. FAIL otherwise -- and then nothing may be written.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <emmc-area.hpp>
#include <emmc.hpp>
#include <hw_result.hpp>
#include <uart.hpp>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace os::rtos;

namespace
{
  constexpr std::uint64_t GiB = 1ull << 30;
  constexpr std::uint64_t AREA_START = 40 * GiB / 512; // LBA 83886080
  constexpr std::uint64_t MARGIN = 2048;                // 1 MiB

  void
  out (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

  void
  out (const char* fmt, ...)
  {
    char b[160];
    va_list ap;
    va_start (ap, fmt);
    std::vsnprintf (b, sizeof (b), fmt, ap);
    va_end (ap);
    uart::write (b);
  }

  std::uint64_t
  le64 (const std::uint8_t* p)
  {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
      {
        v = (v << 8) | p[i];
      }
    return v;
  }

  std::uint32_t
  le32 (const std::uint8_t* p)
  {
    return static_cast<std::uint32_t> (p[0]) | (p[1] << 8) | (p[2] << 16)
           | (static_cast<std::uint32_t> (p[3]) << 24);
  }

  // A GPT GUID, mixed-endian as printed by Linux.
  void
  guid (char* s, std::size_t n, const std::uint8_t* g)
  {
    std::snprintf (s, n,
                   "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-"
                   "%02X%02X%02X%02X%02X%02X",
                   g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9],
                   g[10], g[11], g[12], g[13], g[14], g[15]);
  }

  bool
  fail (const char* what)
  {
    out ("emmc: %s: %s\n", what, emmc::last_error ());
    return false;
  }

  std::uint8_t sector[512];
  std::uint8_t entries[128 * 128]; // up to 128 entries of 128 bytes

  bool
  probe (void)
  {
    emmc::info card;
    if (emmc::init (card) != emmc::status::ok)
      {
        return fail ("init");
      }
    out ("emmc: controller VERID 0x%08x\n", static_cast<unsigned> (card.verid));
    out ("emmc: card MID 0x%02x \"%s\", OCR 0x%08x, EXT_CSD rev %u\n",
         card.manufacturer, card.product, static_cast<unsigned> (card.ocr),
         card.ext_csd_rev);
    out ("emmc: %" PRIu64 " sectors = %" PRIu64 " MiB (%.2f GiB)\n",
         card.sectors, card.sectors / 2048,
         static_cast<double> (card.sectors) * 512.0 / static_cast<double> (GiB));

    // LBA 0: the protective MBR.
    if (emmc::read (0, sector, 1) != emmc::status::ok)
      {
        return fail ("read LBA 0");
      }
    out ("emmc: LBA 0 signature %02x%02x, partition type 0x%02x\n",
         sector[510], sector[511], sector[446 + 4]);

    // LBA 1: the GPT header.
    if (emmc::read (1, sector, 1) != emmc::status::ok)
      {
        return fail ("read LBA 1");
      }
    if (std::memcmp (sector, "EFI PART", 8) != 0)
      {
        out ("emmc: no GPT header at LBA 1 -- refusing to choose an area\n");
        return false;
      }
    const std::uint64_t my_lba = le64 (sector + 24);
    const std::uint64_t alt_lba = le64 (sector + 32);
    const std::uint64_t first_usable = le64 (sector + 40);
    const std::uint64_t last_usable = le64 (sector + 48);
    const std::uint64_t entries_lba = le64 (sector + 72);
    const std::uint32_t n_entries = le32 (sector + 80);
    const std::uint32_t entry_size = le32 (sector + 84);
    out ("emmc: GPT header LBA %" PRIu64 ", backup LBA %" PRIu64
         ", usable %" PRIu64 "..%" PRIu64 ", %u entries x %u bytes at LBA %"
         PRIu64 "\n",
         my_lba, alt_lba, first_usable, last_usable,
         static_cast<unsigned> (n_entries), static_cast<unsigned> (entry_size),
         entries_lba);
    if (entry_size != 128 || n_entries == 0 || n_entries > 128)
      {
        out ("emmc: unexpected GPT entry layout -- refusing\n");
        return false;
      }
    if (alt_lba + 1 != card.sectors)
      {
        out ("emmc: note: backup GPT is not on the last sector (%" PRIu64
             " of %" PRIu64 ")\n",
             alt_lba, card.sectors);
      }

    const std::uint32_t blocks = (n_entries * entry_size + 511) / 512;
    if (emmc::read (entries_lba, entries, blocks) != emmc::status::ok)
      {
        return fail ("read GPT entries");
      }

    std::uint64_t area_end = last_usable + 1; // exclusive
    if (area_end > alt_lba)
      {
        area_end = alt_lba;
      }
    area_end = (area_end - MARGIN) & ~static_cast<std::uint64_t> (2047);

    bool overlap = false;
    std::uint64_t highest = 0;
    for (std::uint32_t i = 0; i < n_entries; ++i)
      {
        const std::uint8_t* e = entries + i * entry_size;
        bool used = false;
        for (unsigned k = 0; k < 16; ++k)
          {
            used = used || (e[k] != 0);
          }
        if (!used)
          {
            continue;
          }
        const std::uint64_t first = le64 (e + 32);
        const std::uint64_t last = le64 (e + 40);
        char name[37];
        for (unsigned k = 0; k < 36; ++k)
          {
            const std::uint8_t c = e[56 + 2 * k];
            name[k] = (c >= 0x20 && c < 0x7F) ? static_cast<char> (c) : '\0';
            if (c == 0)
              {
                break;
              }
          }
        name[36] = '\0';
        char type[40];
        guid (type, sizeof (type), e);
        out ("emmc: partition %u: LBA %" PRIu64 "..%" PRIu64 " (%" PRIu64
             " MiB) type %s \"%s\"\n",
             static_cast<unsigned> (i + 1), first, last,
             (last - first + 1) / 2048, type, name);
        if (last > highest)
          {
            highest = last;
          }
        if (!(last < AREA_START || first >= area_end))
          {
            overlap = true;
            out ("emmc:   ^ OVERLAPS the area from 40 GiB\n");
          }
      }

    out ("emmc: highest partition sector: LBA %" PRIu64 " (%.2f GiB)\n",
         highest, static_cast<double> (highest + 1) * 512.0
                      / static_cast<double> (GiB));
    out ("emmc: the space from 40 GiB is %s\n",
         overlap ? "NOT free (a partition reaches into it)" : "free");

    emmc_area::area a;
    const char* why = "";
    if (!emmc_area::find_area (card, a, why))
      {
        out ("emmc: no area for a filesystem: %s\n", why);
        return false;
      }
    out ("emmc: area for emmc-fs-test: LBA %" PRIu64 "..%" PRIu64 " = %" PRIu64
         " MiB (%.2f..%.2f GiB), after the last partition, before the backup "
         "GPT\n",
         a.start, a.end - 1, (a.end - a.start) / 2048,
         static_cast<double> (a.start) / 2097152.0,
         static_cast<double> (a.end) / 2097152.0);
    return true;
  }
} // namespace

int
os_main (int, char*[])
{
  uart::init (0);
  uart::write ("\nemmc-probe (read only) on " PORT_BANNER_LONG "\n");

  const bool ok = probe ();

  out ("\nRESULT: %s (emmc-probe)\n", ok ? "PASS" : "FAIL");
  if (ok)
    {
      hw_result::ok ();
    }
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
  return 0;
}
