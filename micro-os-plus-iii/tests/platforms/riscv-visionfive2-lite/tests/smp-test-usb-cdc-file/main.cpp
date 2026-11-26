/*
 * smp-test-usb-cdc-file (VisionFive 2 Lite) -- a file round trip over the
 * USB-C CDC-ACM port: the PC sends a file, the board receives all of it into
 * DDR and sends it back, the PC saves it as <name>.back and compares.
 *
 * TinyUSB with the board's Cadence USBSS-DRD driver, CDC API used directly
 * (binary: no echo, no line glue, nothing sent before the PC's header -- so
 * a host-side tty echo has nothing to reflect). On all four U74 (auto-start);
 * the USB thread is pinned to CPU 0. The host side is
 * test/boards/visionfive2-lite/usb-file-host.py.
 *
 * Protocol (lengths and CRC little-endian 32-bit, CRC-32/ISO-HDLC as zlib):
 *   PC -> board   "UOSF" length  <length bytes>
 *   board -> PC   "UOSB" length crc32  <the same length bytes>
 *   PC -> board   'K' (file.back identical to file) or 'X'
 * The whole file is held in a DDR buffer (up to 4 MiB); TinyUSB's CDC FIFOs
 * and the endpoint DMA buffer double-buffer each direction below it.
 *
 * PASS on 'K'. FAIL on 'X', a bad header, a length above the buffer, the
 * bring-up, or no progress for 60 s. The UART shows sizes, CRC, times and
 * throughput.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <clint.hpp>
#include <led.hpp>
#include <riscv-board.hpp>
#include <riscv-smp.hpp>
#include <uart.hpp>
#include <vf2-usb.h>

#include <hw_result.hpp>

#include "tusb.h"
#include "device/usbd_pvt.h"

extern "C" std::uint32_t dcd_cdns3_ep_done (std::uint8_t ep_addr);
extern "C" bool dcd_cdns3_ep_busy (std::uint8_t ep_addr);

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace os::rtos;

namespace
{
  constexpr std::uint32_t BUF_SIZE = 4u * 1024u * 1024u;
  std::uint8_t file_buf[BUF_SIZE] __attribute__ ((aligned (64)));

  enum class stage
  {
    header,
    data,
    send_header,
    send_data,
    ack,
    done
  };

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
  seconds (std::uint64_t t0, std::uint64_t t1)
  {
    return static_cast<double> (t1 - t0)
           / static_cast<double> (riscv_board::TIMEBASE_HZ);
  }

  // CRC-32/ISO-HDLC (zlib's crc32), table-less nibble version.
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

  void*
  usb_file_task (void*)
  {
    this_thread::thread ().cpu_affinity (1u << 0);
    while (port_cpu_id () != 0)
      {
        this_thread::yield ();
      }
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
    std::uint32_t len = 0;
    std::uint32_t got = 0;
    std::uint32_t sent = 0;
    std::uint32_t crc = 0;
    bool mounted = false;
    std::uint64_t t_rx0 = 0, t_rx1 = 0, t_tx0 = 0, t_tx1 = 0;
    std::uint64_t last_progress = clint::now ();
    const std::uint64_t stall
        = 60ull * riscv_board::TIMEBASE_HZ; // 60 s without progress

    for (;;)
      {
        tud_int_handler (0); // polled controller
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
                hdr_got += tud_cdc_read (hdr + hdr_got, 8 - hdr_got);
                last_progress = now;
                if (hdr_got == 8)
                  {
                    if (std::memcmp (hdr, "UOSF", 4) != 0)
                      {
                        finish (false, "bad header magic");
                      }
                    len = le32 (hdr + 4);
                    out ("[file] receiving %u bytes\n",
                         static_cast<unsigned> (len));
                    if (len == 0 || len > BUF_SIZE)
                      {
                        finish (false, "length 0 or above the 4 MiB buffer");
                      }
                    t_rx0 = now;
                    st = stage::data;
                  }
              }
            break;

          case stage::data:
            if (tud_cdc_available ())
              {
                // tud_cdc_read() passes the size on as 16 bits
                // (tu_fifo_read_n): ask for at most 32 KiB at a time --
                // 1048576 would become 0 and nothing would ever be read.
                const std::uint32_t want
                    = (len - got < 32768u) ? len - got : 32768u;
                got += tud_cdc_read (file_buf + got, want);
                last_progress = now;
                if (got == len)
                  {
                    t_rx1 = clint::now ();
                    crc = crc32 (file_buf, len);
                    const double s = seconds (t_rx0, t_rx1);
                    out ("[file] received %u bytes in %.2f s (%.0f KiB/s), "
                         "CRC-32 %08x\n",
                         static_cast<unsigned> (len), s,
                         len / 1024.0 / (s > 0 ? s : 1e-9),
                         static_cast<unsigned> (crc));
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
                  const std::uint32_t n
                      = (len - sent < room) ? len - sent : room;
                  sent += tud_cdc_write (file_buf + sent,
                                         n < 32768u ? n : 32768u);
                  tud_cdc_write_flush ();
                  last_progress = now;
                }
              if (sent == len)
                {
                  t_tx1 = clint::now ();
                  const double s = seconds (t_tx0, t_tx1);
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
                if (a == 'K')
                  {
                    finish (true, "the PC's copy is identical");
                  }
                finish (false, "the PC's copy differs");
              }
            break;

          case stage::done:
            break;
          }

        // Progress, every 2 s while a transfer is under way.
        static std::uint64_t last_report = 0;
        if ((st == stage::data || st == stage::send_data)
            && now - last_report > 2ull * riscv_board::TIMEBASE_HZ)
          {
            last_report = now;
            out ("[file] got %u sent %u | rx fifo %u | OUT done %u, "
                 "usbd busy %d, dcd busy %d | IN done %u\n",
                 static_cast<unsigned> (got), static_cast<unsigned> (sent),
                 static_cast<unsigned> (tud_cdc_available ()),
                 static_cast<unsigned> (dcd_cdns3_ep_done (0x02)),
                 usbd_edpt_busy (0, 0x02) ? 1 : 0,
                 dcd_cdns3_ep_busy (0x02) ? 1 : 0,
                 static_cast<unsigned> (dcd_cdns3_ep_done (0x82)));
          }
        if (now - last_progress > stall)
          {
            out ("[file] stage %d, %u/%u received, %u sent\n",
                 static_cast<int> (st), static_cast<unsigned> (got),
                 static_cast<unsigned> (len), static_cast<unsigned> (sent));
            finish (false, "no progress for 60 s");
          }
        this_thread::yield ();
      }
    return nullptr;
  }

  thread::stack::element_t usb_stack[8192];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::write ("\n=============================================\n");
  uart::write ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  uart::write ("  smp-test-usb-cdc-file : file round trip over USB CDC\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = usb_stack;
  attr.th_stack_size_bytes = sizeof (usb_stack);
  static thread usb_th{ "usb", usb_file_task, nullptr, attr };

  for (;;)
    {
      led::toggle ();
      sysclock.sleep_for (500);
    }
  return 0;
}
