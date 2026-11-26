/*
 * emmc-fs-test (VisionFive 2 Lite) -- a FAT filesystem on the eMMC, in the
 * space after Linux's partitions, with FatFs (Chan, 0.13.1, the xPack the
 * kernel harness uses).
 *
 * The area comes from emmc-area.hpp: after the highest GPT partition, up to
 * the GPT's last usable sector minus 1 MiB (measured: LBA 109929472 on, about
 * 5.8 GiB). The disk layer below maps the filesystem's sector n to the eMMC's
 * LBA area.start + n and refuses anything beyond area.end: FatFs cannot
 * reach a partition or the GPT. The partition table is never written, so
 * Linux does not see this filesystem.
 *
 * The run:
 *   1. find the area; hash what must not change (both GPT copies, the
 *      u-boot-spl and u-boot partitions, the first 4 MiB of the vfat boot
 *      partition, the first and last MiB of the Linux root);
 *   2. mount; with no filesystem there (the first run), format it FAT32
 *      (32 KiB clusters, no partition table inside the area);
 *   3. RUNS.TXT counts the runs: the data outlives a run;
 *   4. write, read back and compare: a text file, 64 KiB and 1 MiB patterns
 *      (seeded with the run number) in /uos; list /uos; delete the text file
 *      and check it is gone; report the free space;
 *   5. unmount, mount again and compare the 1 MiB file from the card;
 *   6. hash the protected sectors again: they must be unchanged.
 *
 * It runs with the other three U74 up (smp-autostart.cpp); FatFs is used from
 * this one thread only (FF_FS_REENTRANT 0).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <emmc-area.hpp>
#include <emmc.hpp>
#include <hw_result.hpp>
#include <uart.hpp>

#include <clint.hpp>
#include <riscv-board.hpp>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C"
{
#include <chan-fatfs/diskio.h>
#include <chan-fatfs/ff.h>
}

using namespace os::rtos;

namespace
{
  emmc::info card;
  emmc_area::area area;
  bool disk_ready = false;

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

  double
  seconds_since (std::uint64_t t0)
  {
    return static_cast<double> (clint::now () - t0)
           / static_cast<double> (riscv_board::TIMEBASE_HZ);
  }

  bool
  bad (const char* what, int code)
  {
    out ("FAIL: %s (%d)%s%s\n", what, code,
         emmc::last_error ()[0] ? ", eMMC: " : "", emmc::last_error ());
    return false;
  }
} // namespace

// ---- The FatFs disk layer: drive 0 = the area -----------------------------

extern "C" DSTATUS
disk_initialize (BYTE pdrv)
{
  return (pdrv == 0 && disk_ready) ? 0 : STA_NOINIT;
}

extern "C" DSTATUS
disk_status (BYTE pdrv)
{
  return (pdrv == 0 && disk_ready) ? 0 : STA_NOINIT;
}

namespace
{
  bool
  in_area (DWORD sector, UINT count)
  {
    const std::uint64_t size = area.end - area.start;
    return count != 0 && sector < size && count <= size - sector;
  }
} // namespace

extern "C" DRESULT
disk_read (BYTE pdrv, BYTE* buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || !disk_ready)
    {
      return RES_NOTRDY;
    }
  if (!in_area (sector, count))
    {
      return RES_PARERR;
    }
  return (emmc::read (area.start + sector, buff, count) == emmc::status::ok)
             ? RES_OK
             : RES_ERROR;
}

extern "C" DRESULT
disk_write (BYTE pdrv, const BYTE* buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || !disk_ready)
    {
      return RES_NOTRDY;
    }
  if (!in_area (sector, count))
    {
      return RES_PARERR; // never outside the area
    }
  return (emmc::write (area.start + sector, buff, count) == emmc::status::ok)
             ? RES_OK
             : RES_ERROR;
}

extern "C" DRESULT
disk_ioctl (BYTE pdrv, BYTE cmd, void* buff)
{
  if (pdrv != 0 || !disk_ready)
    {
      return RES_NOTRDY;
    }
  switch (cmd)
    {
    case CTRL_SYNC:
      return RES_OK; // every write waits until the card has programmed it
    case GET_SECTOR_COUNT:
      *static_cast<DWORD*> (buff) = static_cast<DWORD> (area.end - area.start);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*> (buff) = emmc_area::MiB_SECTORS; // align to 1 MiB
      return RES_OK;
    default:
      return RES_PARERR;
    }
}

// No RTC: a fixed time stamp (2026-11-07 12:00:00).
extern "C" DWORD
get_fattime (void)
{
  return (static_cast<DWORD> (2026 - 1980) << 25) | (11u << 21) | (7u << 16)
         | (12u << 11);
}

// ---- The test ---------------------------------------------------------------

namespace
{
  FATFS fs;
  FIL file;
  std::uint8_t work[32 * 1024]; // f_mkfs
  std::uint8_t wbuf[64 * 1024];
  std::uint8_t rbuf[64 * 1024];

  // The sectors that must not change, hashed before and after.
  bool
  hash_protected (std::uint64_t& h)
  {
    h = 0xcbf29ce484222325ull;
    bool ok = emmc_area::hash_sectors (0, 34, h) // MBR + primary GPT
              && emmc_area::hash_sectors (area.last_usable + 1,
                                          area.card_sectors - area.last_usable - 1,
                                          h); // backup GPT
    for (std::uint32_t i = 0; ok && i < area.n_partitions; ++i)
      {
        const emmc_area::partition& p = area.parts[i];
        const std::uint64_t len = p.last - p.first + 1;
        const std::uint64_t mib = emmc_area::MiB_SECTORS;
        if (len <= 8 * mib)
          {
            ok = emmc_area::hash_sectors (p.first, len, h); // whole (u-boot)
          }
        else if (len <= 512 * mib)
          {
            ok = emmc_area::hash_sectors (p.first, 4 * mib, h); // vfat start
          }
        else
          {
            ok = emmc_area::hash_sectors (p.first, mib, h)
                 && emmc_area::hash_sectors (p.last + 1 - mib, mib, h);
          }
      }
    return ok;
  }

  void
  fill (std::uint8_t* b, std::uint32_t n, std::uint32_t seed, std::uint32_t off)
  {
    for (std::uint32_t i = 0; i < n; ++i)
      {
        const std::uint32_t x = (off + i) * 2654435761u + seed * 40503u;
        b[i] = static_cast<std::uint8_t> (x >> 13);
      }
  }

  bool
  write_pattern (const char* path, std::uint32_t size, std::uint32_t seed)
  {
    FRESULT r = f_open (&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK)
      {
        return bad ("f_open for write", r);
      }
    const std::uint64_t t0 = clint::now ();
    for (std::uint32_t off = 0; off < size; off += sizeof (wbuf))
      {
        const std::uint32_t n
            = (size - off < sizeof (wbuf)) ? size - off : sizeof (wbuf);
        fill (wbuf, n, seed, off);
        UINT bw = 0;
        r = f_write (&file, wbuf, n, &bw);
        if (r != FR_OK || bw != n)
          {
            f_close (&file);
            return bad ("f_write", r);
          }
      }
    r = f_close (&file);
    if (r != FR_OK)
      {
        return bad ("f_close", r);
      }
    const double s = seconds_since (t0);
    out ("  wrote %-22s %7u bytes in %.2f s (%.0f KiB/s)\n", path,
         static_cast<unsigned> (size), s, size / 1024.0 / (s > 0 ? s : 1e-9));
    return true;
  }

  bool
  check_pattern (const char* path, std::uint32_t size, std::uint32_t seed)
  {
    FRESULT r = f_open (&file, path, FA_READ);
    if (r != FR_OK)
      {
        return bad ("f_open for read", r);
      }
    if (f_size (&file) != size)
      {
        f_close (&file);
        out ("FAIL: %s is %u bytes, expected %u\n", path,
             static_cast<unsigned> (f_size (&file)),
             static_cast<unsigned> (size));
        return false;
      }
    const std::uint64_t t0 = clint::now ();
    for (std::uint32_t off = 0; off < size; off += sizeof (rbuf))
      {
        const std::uint32_t n
            = (size - off < sizeof (rbuf)) ? size - off : sizeof (rbuf);
        UINT br = 0;
        r = f_read (&file, rbuf, n, &br);
        if (r != FR_OK || br != n)
          {
            f_close (&file);
            return bad ("f_read", r);
          }
        fill (wbuf, n, seed, off);
        if (std::memcmp (rbuf, wbuf, n) != 0)
          {
            f_close (&file);
            out ("FAIL: %s differs in bytes %u..%u\n", path,
                 static_cast<unsigned> (off), static_cast<unsigned> (off + n));
            return false;
          }
      }
    f_close (&file);
    const double s = seconds_since (t0);
    out ("  read  %-22s %7u bytes in %.2f s (%.0f KiB/s), identical\n", path,
         static_cast<unsigned> (size), s, size / 1024.0 / (s > 0 ? s : 1e-9));
    return true;
  }

  bool
  run (void)
  {
    if (emmc::init (card) != emmc::status::ok)
      {
        return bad ("emmc::init", 0);
      }
    out ("emmc: \"%s\", %" PRIu64 " sectors (%.2f GiB)\n", card.product,
         card.sectors, static_cast<double> (card.sectors) / 2097152.0);

    const char* why = "";
    if (!emmc_area::find_area (card, area, why))
      {
        out ("FAIL: no safe area: %s -- nothing written\n", why);
        return false;
      }
    for (std::uint32_t i = 0; i < area.n_partitions; ++i)
      {
        out ("  partition %u: LBA %" PRIu64 "..%" PRIu64 " \"%s\" (kept)\n",
             static_cast<unsigned> (i + 1), area.parts[i].first,
             area.parts[i].last, area.parts[i].name);
      }
    out ("area: LBA %" PRIu64 "..%" PRIu64 " = %" PRIu64
         " MiB, after the last partition, before the backup GPT\n",
         area.start, area.end - 1, (area.end - area.start) / 2048);

    std::uint64_t h_before = 0;
    if (!hash_protected (h_before))
      {
        return bad ("hash of the protected sectors", 0);
      }
    out ("protected sectors hash: %016" PRIx64 "\n", h_before);

    disk_ready = true;

    // 2. Mount, or format on the first run.
    FRESULT r = f_mount (&fs, "", 1);
    if (r == FR_NO_FILESYSTEM)
      {
        out ("no filesystem in the area: formatting FAT32...\n");
        const std::uint64_t t0 = clint::now ();
        r = f_mkfs ("", FM_FAT32 | FM_SFD, 32768, work, sizeof (work));
        if (r != FR_OK)
          {
            return bad ("f_mkfs", r);
          }
        out ("  formatted in %.1f s\n", seconds_since (t0));
        r = f_mount (&fs, "", 1);
      }
    if (r != FR_OK)
      {
        return bad ("f_mount", r);
      }
    out ("mounted: %s, cluster %u bytes\n",
         fs.fs_type == FS_FAT32 ? "FAT32"
         : fs.fs_type == FS_EXFAT ? "exFAT"
                                  : "FAT12/16",
         static_cast<unsigned> (fs.csize * 512));

    // 3. The run counter.
    unsigned runs = 0;
    if (f_open (&file, "RUNS.TXT", FA_READ) == FR_OK)
      {
        char b[16] = {};
        UINT br = 0;
        f_read (&file, b, sizeof (b) - 1, &br);
        f_close (&file);
        std::sscanf (b, "%u", &runs);
      }
    ++runs;
    {
      char b[16];
      const int n = std::snprintf (b, sizeof (b), "%u\n", runs);
      UINT bw = 0;
      r = f_open (&file, "RUNS.TXT", FA_CREATE_ALWAYS | FA_WRITE);
      if (r == FR_OK)
        {
          r = f_write (&file, b, static_cast<UINT> (n), &bw);
          const FRESULT c = f_close (&file);
          r = (r == FR_OK) ? c : r;
        }
      if (r != FR_OK)
        {
          return bad ("write RUNS.TXT", r);
        }
    }
    out ("run %u on this filesystem (RUNS.TXT)\n", runs);

    // 4. Files.
    r = f_mkdir ("uos");
    if (r != FR_OK && r != FR_EXIST)
      {
        return bad ("f_mkdir uos", r);
      }
    {
      char text[96];
      const int n = std::snprintf (
          text, sizeof (text),
          "uOS++ on the VisionFive 2 Lite, 4 x U74: run %u\n", runs);
      UINT bw = 0;
      r = f_open (&file, "uos/hello.txt", FA_CREATE_ALWAYS | FA_WRITE);
      if (r == FR_OK)
        {
          r = f_write (&file, text, static_cast<UINT> (n), &bw);
          f_close (&file);
        }
      if (r != FR_OK)
        {
          return bad ("write uos/hello.txt", r);
        }
      char back[96] = {};
      UINT br = 0;
      r = f_open (&file, "uos/hello.txt", FA_READ);
      if (r == FR_OK)
        {
          r = f_read (&file, back, sizeof (back) - 1, &br);
          f_close (&file);
        }
      if (r != FR_OK || br != static_cast<UINT> (n)
          || std::memcmp (back, text, static_cast<std::size_t> (n)) != 0)
        {
          return bad ("read back uos/hello.txt", r);
        }
      out ("  uos/hello.txt: %s", back);
    }
    if (!write_pattern ("uos/pattern-64k.bin", 64 * 1024, runs)
        || !write_pattern ("uos/pattern-1m.bin", 1024 * 1024, runs + 1)
        || !check_pattern ("uos/pattern-64k.bin", 64 * 1024, runs)
        || !check_pattern ("uos/pattern-1m.bin", 1024 * 1024, runs + 1))
      {
        return false;
      }

    DIR dir;
    FILINFO fi;
    r = f_opendir (&dir, "uos");
    if (r != FR_OK)
      {
        return bad ("f_opendir uos", r);
      }
    out ("  /uos:\n");
    unsigned entries = 0;
    while (f_readdir (&dir, &fi) == FR_OK && fi.fname[0] != '\0')
      {
        out ("    %-24s %8u\n", fi.fname, static_cast<unsigned> (fi.fsize));
        ++entries;
      }
    f_closedir (&dir);
    if (entries != 3)
      {
        out ("FAIL: /uos holds %u entries, expected 3\n", entries);
        return false;
      }

    r = f_unlink ("uos/hello.txt");
    if (r != FR_OK)
      {
        return bad ("f_unlink uos/hello.txt", r);
      }
    if (f_stat ("uos/hello.txt", &fi) != FR_NO_FILE)
      {
        out ("FAIL: uos/hello.txt still there after f_unlink\n");
        return false;
      }
    out ("  uos/hello.txt deleted\n");

    FATFS* pfs = nullptr;
    DWORD free_clusters = 0;
    r = f_getfree ("", &free_clusters, &pfs);
    if (r != FR_OK)
      {
        return bad ("f_getfree", r);
      }
    out ("  free: %" PRIu64 " MiB of %" PRIu64 " MiB\n",
         static_cast<std::uint64_t> (free_clusters) * pfs->csize / 2048,
         static_cast<std::uint64_t> (pfs->n_fatent - 2) * pfs->csize / 2048);

    // 5. From the card, not from FatFs' window.
    f_mount (nullptr, "", 0);
    r = f_mount (&fs, "", 1);
    if (r != FR_OK)
      {
        return bad ("f_mount again", r);
      }
    if (!check_pattern ("uos/pattern-1m.bin", 1024 * 1024, runs + 1))
      {
        return false;
      }
    f_mount (nullptr, "", 0);
    disk_ready = false;

    // 6. Nothing outside the area changed.
    std::uint64_t h_after = 0;
    if (!hash_protected (h_after))
      {
        return bad ("hash of the protected sectors", 0);
      }
    out ("protected sectors hash: %016" PRIx64 " -- %s\n", h_after,
         h_after == h_before ? "unchanged" : "CHANGED");
    return h_after == h_before;
  }
} // namespace

int
os_main (int, char*[])
{
  uart::init (0);
  uart::write ("\nemmc-fs-test on " PORT_BANNER_LONG "\n");

  const bool ok = run ();

  out ("\nRESULT: %s (emmc-fs-test)\n", ok ? "PASS" : "FAIL");
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
