/*
 * usb-cdc-stream (WCH CH32V307V-EVT-R1) -- a file round trip over the
 * high-speed USB port P6, streamed: the board has 64 KiB of SRAM, so it
 * sends each piece back as it arrives instead of holding the file.
 *
 * TinyUSB with its WCH USBHS driver (portable/wch), the CDC API used
 * directly (binary; nothing is sent before the PC's header, so a host tty
 * echo has nothing to reflect). The PC side is usb-stream-host.py, beside
 * this file, with the test file name.dat.
 *
 * Protocol (little-endian 32-bit; CRC-32/ISO-HDLC, as zlib.crc32):
 *   PC -> board   "UOSF" length  <length bytes>
 *   board -> PC   "UOSE" length  <the same bytes, as they arrive>
 *                 "UOSC" crc32   (of what the board received)
 *   PC -> board   'K' (name.dat.back identical, CRCs agree) or 'X'
 *
 * PASS on 'K'. FAIL on 'X', a bad header, the bring-up, no mount within
 * 120 s, or no progress for 60 s. The console shows the link speed, sizes,
 * CRC, time and throughput.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <ch32-usb.h>
#include <hw_result.hpp>
#include <led.hpp>

#include "tusb.h"

using namespace os::rtos;

namespace
{
  std::uint32_t crc_table[256];

  void
  crc_init (void)
  {
    for (std::uint32_t i = 0; i < 256; ++i)
      {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k)
          {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
          }
        crc_table[i] = c;
      }
  }

  std::uint32_t
  crc_update (std::uint32_t crc, const std::uint8_t* p, std::size_t n)
  {
    crc = ~crc;
    while (n-- != 0)
      {
        crc = crc_table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
      }
    return ~crc;
  }

  void
  put32 (std::uint8_t* b, std::uint32_t v)
  {
    b[0] = static_cast<std::uint8_t> (v);
    b[1] = static_cast<std::uint8_t> (v >> 8);
    b[2] = static_cast<std::uint8_t> (v >> 16);
    b[3] = static_cast<std::uint8_t> (v >> 24);
  }

  std::uint32_t
  get32 (const std::uint8_t* b)
  {
    return static_cast<std::uint32_t> (b[0])
           | (static_cast<std::uint32_t> (b[1]) << 8)
           | (static_cast<std::uint32_t> (b[2]) << 16)
           | (static_cast<std::uint32_t> (b[3]) << 24);
  }

  // Run TinyUSB: its events, or a 1 ms sleep when there are none.
  void
  pump (void)
  {
    if (tud_task_event_ready ())
      {
        tud_task ();
      }
    else
      {
        sysclock.sleep_for (1);
      }
  }

  // Write all of buf to the CDC IN side, pumping meanwhile.
  bool
  write_all (const std::uint8_t* buf, std::uint32_t n, std::uint32_t& idle_ms)
  {
    while (n != 0)
      {
        const std::uint32_t k = tud_cdc_write (buf, n);
        if (k != 0)
          {
            buf += k;
            n -= k;
            idle_ms = 0;
          }
        tud_cdc_write_flush ();
        if (n != 0)
          {
            pump ();
            if (++idle_ms > 60000 || !tud_mounted ())
              {
                return false;
              }
          }
      }
    return true;
  }

  std::uint8_t chunk[512];

  bool
  run (void)
  {
    std::printf ("[usb] waiting for the PC to mount the device (P6)...\n");
    std::uint32_t t = 0;
    while (!tud_mounted ())
      {
        pump ();
        if (++t > 120000)
          {
            std::printf ("[usb] not mounted within 120 s\n");
            return false;
          }
      }
    std::printf ("[usb] mounted, %s speed; waiting for the file\n",
                 tud_speed_get () == TUSB_SPEED_HIGH ? "high" : "full");
    std::fflush (stdout);

    // Header: "UOSF" length.
    std::uint8_t hdr[8];
    std::uint32_t got = 0;
    std::uint32_t idle_ms = 0;
    while (got < sizeof (hdr))
      {
        const std::uint32_t k = tud_cdc_read (hdr + got, sizeof (hdr) - got);
        got += k;
        if (k == 0)
          {
            pump ();
            if (++idle_ms > 60000)
              {
                std::printf ("[usb] no header within 60 s\n");
                return false;
              }
          }
      }
    if (std::memcmp (hdr, "UOSF", 4) != 0)
      {
        std::printf ("[usb] bad header\n");
        return false;
      }
    const std::uint32_t len = get32 (hdr + 4);
    std::printf ("[usb] file: %lu bytes\n", static_cast<unsigned long> (len));

    std::uint8_t ack[8];
    std::memcpy (ack, "UOSE", 4);
    put32 (ack + 4, len);
    idle_ms = 0;
    if (!write_all (ack, sizeof (ack), idle_ms))
      {
        return false;
      }

    // The stream: what arrives goes straight back.
    const auto t0 = sysclock.now ();
    std::uint32_t crc = 0;
    std::uint32_t done = 0;
    idle_ms = 0;
    while (done < len)
      {
        std::uint32_t want = len - done;
        if (want > sizeof (chunk))
          {
            want = sizeof (chunk);
          }
        const std::uint32_t k = tud_cdc_read (chunk, want);
        if (k == 0)
          {
            pump ();
            if (++idle_ms > 60000)
              {
                std::printf ("[usb] no progress for 60 s at %lu of %lu\n",
                             static_cast<unsigned long> (done),
                             static_cast<unsigned long> (len));
                return false;
              }
            continue;
          }
        idle_ms = 0;
        crc = crc_update (crc, chunk, k);
        if (!write_all (chunk, k, idle_ms))
          {
            std::printf ("[usb] echo stalled at %lu of %lu\n",
                         static_cast<unsigned long> (done),
                         static_cast<unsigned long> (len));
            return false;
          }
        done += k;
      }
    const auto ms = sysclock.now () - t0;

    std::uint8_t tail[8];
    std::memcpy (tail, "UOSC", 4);
    put32 (tail + 4, crc);
    idle_ms = 0;
    if (!write_all (tail, sizeof (tail), idle_ms))
      {
        return false;
      }
    std::printf ("[usb] streamed %lu bytes back in %lu ms (%lu KiB/s each "
                 "way), CRC-32 %08lx\n",
                 static_cast<unsigned long> (len),
                 static_cast<unsigned long> (ms),
                 static_cast<unsigned long> (
                     ms != 0 ? (len / 1024u) * 1000u / ms : 0u),
                 static_cast<unsigned long> (crc));

    // The PC's verdict.
    std::uint8_t v = 0;
    idle_ms = 0;
    while (tud_cdc_read (&v, 1) == 0)
      {
        pump ();
        if (++idle_ms > 60000)
          {
            std::printf ("[usb] no verdict from the PC within 60 s\n");
            return false;
          }
      }
    std::printf ("[usb] PC: %s\n", v == 'K' ? "file.back identical"
                                            : "file.back DIFFERS");
    return v == 'K';
  }
} // namespace

int
os_main (int, char*[])
{
  std::printf ("\nusb-cdc-stream: a file round trip over USB HS (P6)\n");
  led::init ();
  crc_init ();

  bool ok = false;
  if (!ch32_usb_hw_init ())
    {
      std::printf ("[usb] bring-up failed (clock not 144 MHz from HSE)\n");
    }
  else
    {
      tusb_init ();
      led::on ();
      ok = run ();
      led::off ();
    }

  std::printf ("\nRESULT: %s (usb-cdc-stream)\n", ok ? "PASS" : "FAIL");
  std::fflush (stdout);
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
