/*
 * ram-disk.cpp -- the 512 MiB RAM disk (see ram-disk.hpp) and the FatFs
 * disk layer that puts it on drive 0.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "ram-disk.hpp"

#include <cstring>
#include <initializer_list>

extern "C"
{
#include <chan-fatfs/diskio.h>
#include <chan-fatfs/ff.h>
}

namespace ram_disk
{
  namespace
  {
    bool probed = false;
    bool usable = false;
    const char* error = "not probed";

    volatile std::uint64_t*
    word (std::uintptr_t address)
    {
      return reinterpret_cast<volatile std::uint64_t*> (address);
    }

    // Two patterns, at `high` and 512 MiB below it.  If the upper address
    // were an alias of the lower one (or not memory at all), the first
    // pattern would not survive the second write.
    bool
    distinct_cells (std::uintptr_t high)
    {
      volatile std::uint64_t* upper = word (high);
      volatile std::uint64_t* lower = word (high - size_bytes);
      const std::uint64_t saved_upper = *upper;
      const std::uint64_t saved_lower = *lower;
      *upper = 0x5241'4d44'4953'4b21ull; // "RAMDISK!"
      *lower = 0xa5a5'5a5a'c3c3'3c3cull;
      __asm__ volatile("fence rw, rw" ::: "memory");
      const bool ok = *upper == 0x5241'4d44'4953'4b21ull
                      && *lower == 0xa5a5'5a5a'c3c3'3c3cull;
      *lower = saved_lower;
      *upper = saved_upper;
      return ok;
    }
  } // namespace

  bool
  available (void)
  {
    return usable;
  }

  const char*
  last_error (void)
  {
    return error;
  }

  bool
  probe (void)
  {
    if (probed)
      {
        return usable;
      }
    probed = true;
    // The partners 512 MiB lower (0x48000000, 0x50000000, 0x5ff00000) are
    // above the image's 32 MiB window and unused.
    for (const std::uintptr_t offset :
         { std::uintptr_t{ 0x0800'0000u }, std::uintptr_t{ 0x1000'0000u },
           std::uintptr_t{ 0x1ff0'0000u } })
      {
        if (!distinct_cells (base + offset))
          {
            error = "no distinct memory at 0x60000000";
            usable = false;
            return false;
          }
      }
    usable = true;
    error = "";
    return true;
  }

  bool
  read (std::uint32_t sector, void* buffer, std::uint32_t count)
  {
    if (!usable || sector >= sectors || count > sectors - sector)
      {
        return false;
      }
    std::memcpy (buffer,
                 reinterpret_cast<const void*> (
                     base + static_cast<std::uint64_t> (sector) * sector_size),
                 static_cast<std::size_t> (count) * sector_size);
    return true;
  }

  bool
  write (std::uint32_t sector, const void* buffer, std::uint32_t count)
  {
    if (!usable || sector >= sectors || count > sectors - sector)
      {
        return false;
      }
    std::memcpy (reinterpret_cast<void*> (
                     base + static_cast<std::uint64_t> (sector) * sector_size),
                 buffer, static_cast<std::size_t> (count) * sector_size);
    return true;
  }
} // namespace ram_disk

// ---- FatFs disk layer: drive 0 = the RAM disk --------------------------------

extern "C" DSTATUS
disk_initialize (BYTE pdrv)
{
  return (pdrv == 0 && ram_disk::probe ()) ? 0 : STA_NOINIT;
}

extern "C" DSTATUS
disk_status (BYTE pdrv)
{
  return (pdrv == 0 && ram_disk::available ()) ? 0 : STA_NOINIT;
}

extern "C" DRESULT
disk_read (BYTE pdrv, BYTE* buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || !ram_disk::available ())
    {
      return RES_NOTRDY;
    }
  return ram_disk::read (sector, buff, count) ? RES_OK : RES_PARERR;
}

extern "C" DRESULT
disk_write (BYTE pdrv, const BYTE* buff, DWORD sector, UINT count)
{
  if (pdrv != 0 || !ram_disk::available ())
    {
      return RES_NOTRDY;
    }
  return ram_disk::write (sector, buff, count) ? RES_OK : RES_PARERR;
}

extern "C" DRESULT
disk_ioctl (BYTE pdrv, BYTE cmd, void* buff)
{
  if (pdrv != 0 || !ram_disk::available ())
    {
      return RES_NOTRDY;
    }
  switch (cmd)
    {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_COUNT:
      *static_cast<DWORD*> (buff) = ram_disk::sectors;
      return RES_OK;
    case GET_SECTOR_SIZE:
      *static_cast<WORD*> (buff) = ram_disk::sector_size;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*> (buff) = 8;
      return RES_OK;
    default:
      return RES_PARERR;
    }
}

extern "C" DWORD
get_fattime (void)
{
  // 2026-11-11 12:00 (no RTC is read by this test).
  return (static_cast<DWORD> (2026 - 1980) << 25) | (11u << 21) | (11u << 16)
         | (12u << 11);
}
