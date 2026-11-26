/*
 * smp-test-usb-cdc-file-fs (VisionFive 2 Lite) -- a file round trip over the
 * USB-C CDC-ACM port THROUGH THE eMMC: the PC sends a file, the board saves
 * it on the FAT filesystem in the eMMC area after Linux's partitions, reads
 * it back from the card, and sends that back; the PC saves <name>.back and
 * compares. The UART lists the whole filesystem before and after.
 *
 * Built from smp-test-usb-cdc-file (USB, binary CDC, the board's Cadence
 * cdns3 driver) and emmc-fs-test (the eMMC area, its disk layer and the
 * protection hash). On all four U74; everything here runs in one thread
 * pinned to CPU 0 (FatFs is not reentrant here, FF_FS_REENTRANT 0).
 *
 * Protocol (little-endian 32-bit; CRC-32 as zlib):
 *   PC -> board   "UOSN" length namelen(1) name  <length bytes>
 *   board -> PC   "UOSB" length crc32  <the file, read from the eMMC>
 *   PC -> board   'K' (identical) or 'X'
 *
 * The run:
 *   1. eMMC: find the area (emmc-area.hpp), hash what must not change,
 *      mount (format FAT32 if there is no filesystem), list it;
 *   2. USB: receive the file into DDR;
 *   3. write it to /usb/<name>, close, unmount, CLEAR the DDR buffer,
 *      mount again, read /usb/<name> back into the buffer (so the data
 *      that goes back can only have come from the card), list again;
 *   4. send it back; PASS on 'K' and an unchanged protection hash.
 *
 * The host side is usb-file-host.py in this directory.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <clint.hpp>
#include <emmc-area.hpp>
#include <emmc.hpp>
#include <led.hpp>
#include <riscv-board.hpp>
#include <riscv-smp.hpp>
#include <uart.hpp>
#include <vf2-usb.h>

#include <hw_result.hpp>

#include "tusb.h"

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
  constexpr std::uint32_t BUF_SIZE = 4u * 1024u * 1024u;
  std::uint8_t file_buf[BUF_SIZE] __attribute__ ((aligned (64)));

  emmc::info card;
  emmc_area::area area;
  bool disk_ready = false;

  FATFS fs;
  FIL file;
  std::uint8_t mkfs_work[32 * 1024];

  void
  out (const char* fmt, ...) __attribute__ ((format (printf, 1, 2)));

  void
  out (const char* fmt, ...)
  {
    char b[200];
    va_list ap;
    va_start (ap, fmt);
    std::vsnprintf (b, sizeof (b), fmt, ap);
    va_end (ap);
    uart::write (b);
  }

  double
  seconds (std::uint64_t t0, std::uint64_t t1)
  {
    return static_cast<double> (t1 - t0)
           / static_cast<double> (riscv_board::TIMEBASE_HZ);
  }

  [[noreturn]] void
  finish (bool ok, const char* why)
  {
    out ("\nRESULT: %s (%s)\n", ok ? "PASS" : "FAIL", why);
    if (ok)
      {
        hw_result::ok ();
      }
    hw_result::fail ();
    for (;;)
      {
        sysclock.sleep_for (1000);
      }
  }

  [[noreturn]] void
  fail_fs (const char* what, int code)
  {
    out ("[fs] %s failed (%d)%s%s\n", what, code,
         emmc::last_error ()[0] ? ", eMMC: " : "", emmc::last_error ());
    finish (false, what);
  }
} // namespace

// ---- The FatFs disk layer: drive 0 = the eMMC area (as emmc-fs-test) ------

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
      return RES_OK;
    case GET_SECTOR_COUNT:
      *static_cast<DWORD*> (buff) = static_cast<DWORD> (area.end - area.start);
      return RES_OK;
    case GET_BLOCK_SIZE:
      *static_cast<DWORD*> (buff) = emmc_area::MiB_SECTORS;
      return RES_OK;
    default:
      return RES_PARERR;
    }
}

extern "C" DWORD
get_fattime (void)
{
  return (static_cast<DWORD> (2026 - 1980) << 25) | (11u << 21) | (7u << 16)
         | (12u << 11);
}

// ---- eMMC / filesystem helpers ---------------------------------------------

namespace
{
  bool
  hash_protected (std::uint64_t& h)
  {
    h = 0xcbf29ce484222325ull;
    bool ok = emmc_area::hash_sectors (0, 34, h)
              && emmc_area::hash_sectors (
                  area.last_usable + 1,
                  area.card_sectors - area.last_usable - 1, h);
    for (std::uint32_t i = 0; ok && i < area.n_partitions; ++i)
      {
        const emmc_area::partition& p = area.parts[i];
        const std::uint64_t len = p.last - p.first + 1;
        const std::uint64_t mib = emmc_area::MiB_SECTORS;
        if (len <= 8 * mib)
          {
            ok = emmc_area::hash_sectors (p.first, len, h);
          }
        else if (len <= 512 * mib)
          {
            ok = emmc_area::hash_sectors (p.first, 4 * mib, h);
          }
        else
          {
            ok = emmc_area::hash_sectors (p.first, mib, h)
                 && emmc_area::hash_sectors (p.last + 1 - mib, mib, h);
          }
      }
    return ok;
  }

  // The whole filesystem, recursively: directories and files with sizes.
  void
  list_dir (const char* path, unsigned depth, unsigned& files,
            std::uint64_t& bytes)
  {
    DIR dir;
    FILINFO fi;
    if (f_opendir (&dir, path) != FR_OK)
      {
        return;
      }
    while (f_readdir (&dir, &fi) == FR_OK && fi.fname[0] != '\0')
      {
        char sub[256];
        std::snprintf (sub, sizeof (sub), "%s/%s",
                       (path[0] == '\0' || std::strcmp (path, "/") == 0)
                           ? ""
                           : path,
                       fi.fname);
        if (fi.fattrib & AM_DIR)
          {
            out ("[fs] %*s%s/\n", static_cast<int> (2 * depth), "", fi.fname);
            list_dir (sub, depth + 1, files, bytes);
          }
        else
          {
            out ("[fs] %*s%-28s %10u\n", static_cast<int> (2 * depth), "",
                 fi.fname, static_cast<unsigned> (fi.fsize));
            ++files;
            bytes += fi.fsize;
          }
      }
    f_closedir (&dir);
  }

  void
  list_fs (const char* title)
  {
    out ("[fs] ---- %s: the eMMC filesystem (LBA %" PRIu64 "..) ----\n",
         title, area.start);
    unsigned files = 0;
    std::uint64_t bytes = 0;
    list_dir ("/", 0, files, bytes);
    FATFS* pfs = nullptr;
    DWORD free_clusters = 0;
    if (f_getfree ("", &free_clusters, &pfs) == FR_OK)
      {
        out ("[fs] %u files, %" PRIu64 " bytes; free %" PRIu64
             " MiB of %" PRIu64 " MiB\n",
             files, bytes,
             static_cast<std::uint64_t> (free_clusters) * pfs->csize / 2048,
             static_cast<std::uint64_t> (pfs->n_fatent - 2) * pfs->csize
                 / 2048);
      }
  }

  void
  mount (void)
  {
    FRESULT r = f_mount (&fs, "", 1);
    if (r == FR_NO_FILESYSTEM)
      {
        out ("[fs] no filesystem in the area: formatting FAT32...\n");
        r = f_mkfs ("", FM_FAT32 | FM_SFD, 32768, mkfs_work,
                    sizeof (mkfs_work));
        if (r != FR_OK)
          {
            fail_fs ("f_mkfs", r);
          }
        r = f_mount (&fs, "", 1);
      }
    if (r != FR_OK)
      {
        fail_fs ("f_mount", r);
      }
  }

  void
  emmc_setup (void)
  {
    if (emmc::init (card) != emmc::status::ok)
      {
        fail_fs ("emmc::init", 0);
      }
    const char* why = "";
    if (!emmc_area::find_area (card, area, why))
      {
        out ("[fs] no safe area: %s -- nothing written\n", why);
        finish (false, "no safe eMMC area");
      }
    out ("[fs] eMMC \"%s\", area LBA %" PRIu64 "..%" PRIu64 " (%" PRIu64
         " MiB) after the last partition\n",
         card.product, area.start, area.end - 1,
         (area.end - area.start) / 2048);
    disk_ready = true;
    mount ();
    list_fs ("before");
  }

  // Save buf[0..len) as /usb/<name>, then read it back from the card.
  bool
  save_and_reload (const char* name, std::uint32_t len)
  {
    char path[96];
    std::snprintf (path, sizeof (path), "usb/%s", name);
    FRESULT r = f_mkdir ("usb");
    if (r != FR_OK && r != FR_EXIST)
      {
        fail_fs ("f_mkdir usb", r);
      }

    std::uint64_t t0 = clint::now ();
    r = f_open (&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (r != FR_OK)
      {
        fail_fs ("f_open for write", r);
      }
    for (std::uint32_t off = 0; off < len;)
      {
        const UINT n = (len - off < 32768u) ? len - off : 32768u;
        UINT bw = 0;
        r = f_write (&file, file_buf + off, n, &bw);
        if (r != FR_OK || bw != n)
          {
            f_close (&file);
            fail_fs ("f_write", r);
          }
        off += n;
      }
    r = f_close (&file);
    if (r != FR_OK)
      {
        fail_fs ("f_close", r);
      }
    double s = seconds (t0, clint::now ());
    out ("[fs] wrote /%s: %u bytes in %.2f s (%.0f KiB/s)\n", path,
         static_cast<unsigned> (len), s, len / 1024.0 / (s > 0 ? s : 1e-9));

    // Read back from the card, not from RAM: unmount, clear the buffer,
    // mount again.
    f_mount (nullptr, "", 0);
    std::memset (file_buf, 0, len);
    mount ();

    t0 = clint::now ();
    r = f_open (&file, path, FA_READ);
    if (r != FR_OK)
      {
        fail_fs ("f_open for read", r);
      }
    if (f_size (&file) != len)
      {
        out ("[fs] /%s is %u bytes, expected %u\n", path,
             static_cast<unsigned> (f_size (&file)),
             static_cast<unsigned> (len));
        f_close (&file);
        return false;
      }
    for (std::uint32_t off = 0; off < len;)
      {
        const UINT n = (len - off < 32768u) ? len - off : 32768u;
        UINT br = 0;
        r = f_read (&file, file_buf + off, n, &br);
        if (r != FR_OK || br != n)
          {
            f_close (&file);
            fail_fs ("f_read", r);
          }
        off += n;
      }
    f_close (&file);
    s = seconds (t0, clint::now ());
    out ("[fs] read  /%s: %u bytes in %.2f s (%.0f KiB/s) -- this is what "
         "goes back\n",
         path, static_cast<unsigned> (len), s,
         len / 1024.0 / (s > 0 ? s : 1e-9));
    list_fs ("after");
    return true;
  }

  // ---- protocol helpers ----------------------------------------------------

  std::uint32_t
  crc32 (const std::uint8_t* p, std::uint32_t n)
  {
    static const std::uint32_t t[16]
        = { 0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC,
            0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
            0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
            0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C };
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < n; ++i)
      {
        c ^= p[i];
        c = (c >> 4) ^ t[c & 0xF];
        c = (c >> 4) ^ t[c & 0xF];
      }
    return ~c;
  }

  std::uint32_t
  le32 (const std::uint8_t* p)
  {
    return static_cast<std::uint32_t> (p[0]) | (p[1] << 8) | (p[2] << 16)
           | (static_cast<std::uint32_t> (p[3]) << 24);
  }

  void
  put_le32 (std::uint8_t* p, std::uint32_t v)
  {
    p[0] = static_cast<std::uint8_t> (v);
    p[1] = static_cast<std::uint8_t> (v >> 8);
    p[2] = static_cast<std::uint8_t> (v >> 16);
    p[3] = static_cast<std::uint8_t> (v >> 24);
  }

  // A FAT-safe file name: the PC's base name, other characters as '_'.
  void
  sanitize (char* name)
  {
    for (char* c = name; *c; ++c)
      {
        const bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
                        || (*c >= '0' && *c <= '9') || *c == '.' || *c == '-'
                        || *c == '_';
        if (!ok)
          {
            *c = '_';
          }
      }
    if (name[0] == '\0' || name[0] == '.')
      {
        std::strcpy (name, "file.dat");
      }
  }

  enum class stage
  {
    header,
    name,
    data,
    send_header,
    send_data,
    ack
  };

  void*
  usb_fs_task (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    while (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }

    std::uint64_t h_before = 0;
    emmc_setup ();
    if (!hash_protected (h_before))
      {
        fail_fs ("hash of the protected sectors", 0);
      }
    out ("[fs] protected sectors hash %016" PRIx64 "\n", h_before);

    if (!vf2_usb_hw_init ())
      {
        finish (false, "USB bring-up");
      }
    tusb_init ();
    out ("[usb] tusb_init done (Cadence cdns3, full speed); waiting for the "
         "host\n");

    stage st = stage::header;
    std::uint8_t hdr[12];
    std::uint32_t hdr_got = 0;
    std::uint32_t name_len = 0, name_got = 0;
    char name[80] = {};
    std::uint32_t len = 0, got = 0, sent = 0, crc = 0;
    bool mounted = false;
    std::uint64_t t_rx0 = 0, t_tx0 = 0;
    std::uint64_t last_progress = clint::now ();
    const std::uint64_t stall = 60ull * riscv_board::TIMEBASE_HZ;

    for (;;)
      {
        tud_int_handler (0);
        if (tud_task_event_ready ())
          {
            tud_task ();
          }
        if (tud_mounted () != mounted)
          {
            mounted = tud_mounted ();
            out (mounted ? "[usb] mounted (configured by the host)\n"
                         : "[usb] unmounted\n");
          }
        const std::uint64_t now = clint::now ();

        switch (st)
          {
          case stage::header:
            if (tud_cdc_available ())
              {
                hdr_got += tud_cdc_read (hdr + hdr_got, 9 - hdr_got);
                last_progress = now;
                if (hdr_got == 9)
                  {
                    if (std::memcmp (hdr, "UOSN", 4) != 0)
                      {
                        finish (false, "bad header magic");
                      }
                    len = le32 (hdr + 4);
                    name_len = hdr[8];
                    if (len == 0 || len > BUF_SIZE || name_len == 0
                        || name_len >= sizeof (name))
                      {
                        finish (false, "bad length or name length");
                      }
                    st = stage::name;
                  }
              }
            break;

          case stage::name:
            if (tud_cdc_available ())
              {
                name_got += tud_cdc_read (name + name_got,
                                          name_len - name_got);
                last_progress = now;
                if (name_got == name_len)
                  {
                    name[name_len] = '\0';
                    sanitize (name);
                    out ("[file] receiving \"%s\", %u bytes\n", name,
                         static_cast<unsigned> (len));
                    t_rx0 = now;
                    st = stage::data;
                  }
              }
            break;

          case stage::data:
            if (tud_cdc_available ())
              {
                // tud_cdc_read sizes are 16-bit: 32 KiB at a time.
                const std::uint32_t want
                    = (len - got < 32768u) ? len - got : 32768u;
                got += tud_cdc_read (file_buf + got, want);
                last_progress = now;
                if (got == len)
                  {
                    const double s = seconds (t_rx0, clint::now ());
                    out ("[file] received %u bytes in %.2f s (%.0f KiB/s), "
                         "CRC-32 %08x\n",
                         static_cast<unsigned> (len), s,
                         len / 1024.0 / (s > 0 ? s : 1e-9),
                         static_cast<unsigned> (crc32 (file_buf, len)));
                    if (!save_and_reload (name, len))
                      {
                        finish (false, "the file read back has another size");
                      }
                    crc = crc32 (file_buf, len);
                    out ("[file] CRC-32 of the copy read from the eMMC %08x\n",
                         static_cast<unsigned> (crc));
                    last_progress = clint::now ();
                    st = stage::send_header;
                  }
              }
            break;

          case stage::send_header:
            if (tud_cdc_connected () && tud_cdc_write_available () >= 12)
              {
                std::memcpy (hdr, "UOSB", 4);
                put_le32 (hdr + 4, len);
                put_le32 (hdr + 8, crc);
                tud_cdc_write (hdr, 12);
                tud_cdc_write_flush ();
                t_tx0 = now;
                last_progress = now;
                st = stage::send_data;
              }
            break;

          case stage::send_data:
            {
              const std::uint32_t room = tud_cdc_write_available ();
              if (room)
                {
                  std::uint32_t n = (len - sent < room) ? len - sent : room;
                  n = (n < 32768u) ? n : 32768u;
                  sent += tud_cdc_write (file_buf + sent, n);
                  tud_cdc_write_flush ();
                  last_progress = now;
                }
              if (sent == len)
                {
                  const double s = seconds (t_tx0, clint::now ());
                  out ("[file] sent back %u bytes in %.2f s (%.0f KiB/s)\n",
                       static_cast<unsigned> (len), s,
                       len / 1024.0 / (s > 0 ? s : 1e-9));
                  st = stage::ack;
                }
            }
            break;

          case stage::ack:
            tud_cdc_write_flush ();
            if (tud_cdc_available ())
              {
                std::uint8_t a = 0;
                tud_cdc_read (&a, 1);
                f_mount (nullptr, "", 0);
                std::uint64_t h_after = 0;
                const bool hashed = hash_protected (h_after);
                out ("[fs] protected sectors hash %016" PRIx64 " -- %s\n",
                     h_after,
                     (hashed && h_after == h_before) ? "unchanged" : "CHANGED");
                if (!hashed || h_after != h_before)
                  {
                    finish (false, "sectors outside the area changed");
                  }
                if (a == 'K')
                  {
                    finish (true, "the PC's copy, read from the eMMC, is "
                                  "identical");
                  }
                finish (false, "the PC's copy differs");
              }
            break;
          }

        // A fresh time: the eMMC save and reload above can take a second
        // after `now` was read, and set last_progress later than it --
        // `now - last_progress` would wrap around (unsigned) and trip at
        // once.
        const std::uint64_t t_check = clint::now ();
        if (t_check > last_progress && t_check - last_progress > stall)
          {
            out ("[file] stage %d, %u/%u received, %u sent | mounted %d "
                 "suspended %d ready %d connected %d tx room %u\n",
                 static_cast<int> (st), static_cast<unsigned> (got),
                 static_cast<unsigned> (len), static_cast<unsigned> (sent),
                 tud_mounted () ? 1 : 0, tud_suspended () ? 1 : 0,
                 tud_ready () ? 1 : 0, tud_cdc_connected () ? 1 : 0,
                 static_cast<unsigned> (tud_cdc_write_available ()));
            finish (false, "no progress for 60 s");
          }
        this_thread::yield ();
      }
    return nullptr;
  }

  thread::stack::element_t usb_stack[16384];
} // namespace

// TinyUSB's device events, on the UART (diagnostics).
extern "C" void
tud_suspend_cb (bool remote_wakeup_en)
{
  out ("[usb] suspend (remote wakeup %d)\n", remote_wakeup_en ? 1 : 0);
}

extern "C" void
tud_resume_cb (void)
{
  out ("[usb] resume\n");
}

extern "C" void
tud_cdc_line_state_cb (uint8_t itf, bool dtr, bool rts)
{
  out ("[usb] line state itf %u: DTR %d RTS %d\n", itf, dtr ? 1 : 0,
       rts ? 1 : 0);
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::write ("\n=============================================\n");
  uart::write ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  uart::write ("  smp-test-usb-cdc-file-fs : USB file -> eMMC FAT -> USB\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = usb_stack;
  attr.th_stack_size_bytes = sizeof (usb_stack);
  static thread usb_th{ "usb-fs", usb_fs_task, nullptr, attr };

  for (;;)
    {
      led::toggle ();
      sysclock.sleep_for (500);
    }
  return 0;
}
