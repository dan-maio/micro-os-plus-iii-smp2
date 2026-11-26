/*
 * emmc.cpp -- the VisionFive 2 Lite's eMMC, polled (include/emmc.hpp).
 *
 * Register names and bits are those of Linux's drivers/mmc/host/dw_mmc.h;
 * the clock change follows dw_mci_setup_bus() (CLKENA 0, CLKSRC 0, update,
 * CLKDIV, update, CLKENA, update) and the reset dw_mci_ctrl_reset().
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <emmc.hpp>

#include <clint.hpp>
#include <riscv-board.hpp>

#include <cstdio>
#include <cstring>

namespace
{
  constexpr std::uintptr_t BASE = 0x16010000u;

  // Registers (dw_mmc.h).
  constexpr std::uintptr_t CTRL = 0x000;
  constexpr std::uintptr_t PWREN = 0x004;
  constexpr std::uintptr_t CLKDIV = 0x008;
  constexpr std::uintptr_t CLKSRC = 0x00c;
  constexpr std::uintptr_t CLKENA = 0x010;
  constexpr std::uintptr_t TMOUT = 0x014;
  constexpr std::uintptr_t CTYPE = 0x018;
  constexpr std::uintptr_t BLKSIZ = 0x01c;
  constexpr std::uintptr_t BYTCNT = 0x020;
  constexpr std::uintptr_t INTMASK = 0x024;
  constexpr std::uintptr_t CMDARG = 0x028;
  constexpr std::uintptr_t CMD = 0x02c;
  constexpr std::uintptr_t RESP0 = 0x030;
  constexpr std::uintptr_t RESP1 = 0x034;
  constexpr std::uintptr_t RESP2 = 0x038;
  constexpr std::uintptr_t RESP3 = 0x03c;
  constexpr std::uintptr_t RINTSTS = 0x044;
  constexpr std::uintptr_t STATUS = 0x048;
  constexpr std::uintptr_t FIFOTH = 0x04c;
  constexpr std::uintptr_t VERID = 0x06c;
  constexpr std::uintptr_t BMOD = 0x080;
  constexpr std::uintptr_t DATA_OFFSET = 0x100;      // VERID < 2.40a
  constexpr std::uintptr_t DATA_240A_OFFSET = 0x200; // VERID >= 2.40a

  // CTRL
  constexpr std::uint32_t CTRL_RESET = 1u << 0;
  constexpr std::uint32_t CTRL_FIFO_RESET = 1u << 1;
  constexpr std::uint32_t CTRL_DMA_RESET = 1u << 2;
  // CLKENA
  constexpr std::uint32_t CLKEN_ENABLE = 1u << 0;
  // RINTSTS
  constexpr std::uint32_t INT_EBE = 1u << 15;
  constexpr std::uint32_t INT_ACD = 1u << 14;
  constexpr std::uint32_t INT_SBE = 1u << 13;
  constexpr std::uint32_t INT_HLE = 1u << 12;
  constexpr std::uint32_t INT_FRUN = 1u << 11;
  constexpr std::uint32_t INT_HTO = 1u << 10;
  constexpr std::uint32_t INT_DRTO = 1u << 9;
  constexpr std::uint32_t INT_RTO = 1u << 8;
  constexpr std::uint32_t INT_DCRC = 1u << 7;
  constexpr std::uint32_t INT_RCRC = 1u << 6;
  constexpr std::uint32_t INT_DATA_OVER = 1u << 3;
  constexpr std::uint32_t INT_CMD_DONE = 1u << 2;
  constexpr std::uint32_t INT_RESP_ERR = 1u << 1;
  constexpr std::uint32_t INT_DATA_ERRORS
      = INT_EBE | INT_SBE | INT_HLE | INT_FRUN | INT_DRTO | INT_DCRC;
  // CMD
  constexpr std::uint32_t CMD_START = 1u << 31;
  constexpr std::uint32_t CMD_UPD_CLK = 1u << 21;
  constexpr std::uint32_t CMD_INIT = 1u << 15;
  constexpr std::uint32_t CMD_PRV_DAT_WAIT = 1u << 13;
  constexpr std::uint32_t CMD_SEND_STOP = 1u << 12;
  constexpr std::uint32_t CMD_DAT_WR = 1u << 10;
  constexpr std::uint32_t CMD_DAT_EXP = 1u << 9;
  constexpr std::uint32_t CMD_RESP_CRC = 1u << 8;
  constexpr std::uint32_t CMD_RESP_LONG = 1u << 7;
  constexpr std::uint32_t CMD_RESP_EXP = 1u << 6;
  // STATUS
  constexpr std::uint32_t STATUS_BUSY = 1u << 9;

  // Response types.
  constexpr std::uint32_t R_NONE = 0;
  constexpr std::uint32_t R1 = CMD_RESP_EXP | CMD_RESP_CRC;
  constexpr std::uint32_t R2 = CMD_RESP_EXP | CMD_RESP_LONG | CMD_RESP_CRC;
  constexpr std::uint32_t R3 = CMD_RESP_EXP; // OCR: no CRC

  // The controller clock (measured: PLL2 1188 MHz / 3 / 2), and the two
  // card clocks: ciu / (2 * CLKDIV).
  constexpr std::uint32_t CLKDIV_IDENT = 248; // 399 kHz
  constexpr std::uint32_t CLKDIV_DATA = 4;    // 24.75 MHz

  constexpr std::uint32_t FIFO_DEPTH = 32; // jh7110.dtsi fifo-depth
  constexpr std::uint32_t BLOCK = 512;
  constexpr std::uint32_t RCA = 1;

  std::uintptr_t fifo = BASE + DATA_240A_OFFSET;
  bool ready = false;
  char err[96] = "";

  volatile std::uint32_t&
  reg (std::uintptr_t off)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (BASE + off);
  }

  // The FIFO is 32 bits wide: HCON's H_DATA_WIDTH reads 1 (dw_mci_probe():
  // 0 = 16, 1 = 32, 2 = 64 bits), and STATUS.FIFO_COUNT counts 32-bit
  // words.
  volatile std::uint32_t&
  fifo32 (void)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (fifo);
  }

  std::uint64_t
  ticks (std::uint32_t ms)
  {
    return static_cast<std::uint64_t> (ms) * (riscv_board::TIMEBASE_HZ / 1000);
  }

  void
  delay_ms (std::uint32_t ms)
  {
    const std::uint64_t end = clint::now () + ticks (ms);
    while (clint::now () < end)
      {
      }
  }

  emmc::status
  fail (emmc::status s, unsigned cmd, std::uint32_t rint, const char* what)
  {
    std::snprintf (err, sizeof (err), "CMD%u: %s (RINTSTS 0x%08x)", cmd,
                   what, static_cast<unsigned> (rint));
    return s;
  }

  bool
  ctrl_reset (std::uint32_t bits)
  {
    reg (CTRL) = reg (CTRL) | bits;
    const std::uint64_t end = clint::now () + ticks (500);
    while (reg (CTRL) & bits)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
    return true;
  }

  // mci_send_cmd(): a command the card does not see (clock update).
  bool
  ciu_update (void)
  {
    reg (CMDARG) = 0;
    reg (CMD) = CMD_START | CMD_UPD_CLK | CMD_PRV_DAT_WAIT;
    const std::uint64_t end = clint::now () + ticks (500);
    while (reg (CMD) & CMD_START)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
    return true;
  }

  // dw_mci_setup_bus().
  bool
  set_clock (std::uint32_t div)
  {
    reg (CLKENA) = 0;
    reg (CLKSRC) = 0;
    if (!ciu_update ())
      {
        return false;
      }
    reg (CLKDIV) = div;
    if (!ciu_update ())
      {
        return false;
      }
    reg (CLKENA) = CLKEN_ENABLE;
    return ciu_update ();
  }

  // Wait while the card holds DAT0 low (R1b, programming).
  bool
  wait_not_busy (std::uint32_t ms)
  {
    const std::uint64_t end = clint::now () + ticks (ms);
    while (reg (STATUS) & STATUS_BUSY)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
    return true;
  }

  emmc::status
  command (unsigned idx, std::uint32_t arg, std::uint32_t flags)
  {
    reg (RINTSTS) = 0xFFFFFFFFu;
    reg (CMDARG) = arg;
    __asm__ volatile("fence" ::: "memory");
    reg (CMD) = CMD_START | flags | (idx & 0x3F);

    const std::uint64_t end = clint::now () + ticks (1000);
    std::uint32_t r;
    while (((r = reg (RINTSTS)) & INT_CMD_DONE) == 0)
      {
        if (clint::now () > end)
          {
            return fail (emmc::status::timeout, idx, r, "no CMD_DONE");
          }
      }
    if (r & INT_RTO)
      {
        return fail (emmc::status::timeout, idx, r, "response timeout");
      }
    if ((flags & CMD_RESP_CRC) && (r & INT_RCRC))
      {
        return fail (emmc::status::crc, idx, r, "response CRC");
      }
    if (r & INT_RESP_ERR)
      {
        return fail (emmc::status::error, idx, r, "response error");
      }
    return emmc::status::ok;
  }

  // Move `bytes` between the FIFO and `buf` until DATA_OVER.
  emmc::status
  data_read (unsigned idx, std::uint8_t* buf, std::uint32_t bytes)
  {
    std::uint32_t done = 0;
    const std::uint64_t end = clint::now () + ticks (2000);
    for (;;)
      {
        const std::uint32_t r = reg (RINTSTS);
        if (r & INT_DATA_ERRORS)
          {
            return fail ((r & INT_DCRC) ? emmc::status::crc
                                        : emmc::status::error,
                         idx, r, "data error");
          }
        std::uint32_t n = (reg (STATUS) >> 17) & 0x1FFF; // FIFO entries
        while (n-- && done < bytes)
          {
            const std::uint32_t v = fifo32 ();
            std::memcpy (buf + done, &v, 4);
            done += 4;
          }
        if ((r & INT_DATA_OVER) && done >= bytes)
          {
            return emmc::status::ok;
          }
        if (clint::now () > end)
          {
            return fail (emmc::status::timeout, idx, r, "data timeout");
          }
      }
  }

  emmc::status
  data_write (unsigned idx, const std::uint8_t* buf, std::uint32_t bytes)
  {
    std::uint32_t done = 0;
    const std::uint64_t end = clint::now () + ticks (2000);
    for (;;)
      {
        const std::uint32_t r = reg (RINTSTS);
        if (r & INT_DATA_ERRORS)
          {
            return fail ((r & INT_DCRC) ? emmc::status::crc
                                        : emmc::status::error,
                         idx, r, "data error");
          }
        const std::uint32_t used = (reg (STATUS) >> 17) & 0x1FFF;
        std::uint32_t room = (used < FIFO_DEPTH) ? FIFO_DEPTH - used : 0;
        while (room-- && done < bytes)
          {
            std::uint32_t v;
            std::memcpy (&v, buf + done, 4);
            fifo32 () = v;
            done += 4;
          }
        if (r & INT_DATA_OVER)
          {
            return (done >= bytes)
                       ? emmc::status::ok
                       : fail (emmc::status::error, idx, r, "short write");
          }
        if (clint::now () > end)
          {
            return fail (emmc::status::timeout, idx, r, "data timeout");
          }
      }
  }

  emmc::status
  transfer (unsigned idx, std::uint32_t arg, std::uint8_t* rbuf,
            const std::uint8_t* wbuf, std::uint32_t count)
  {
    if (!wait_not_busy (2000))
      {
        return fail (emmc::status::timeout, idx, reg (RINTSTS), "card busy");
      }
    const std::uint32_t bytes = count * BLOCK;
    if (!ctrl_reset (CTRL_FIFO_RESET))
      {
        return fail (emmc::status::error, idx, reg (CTRL), "FIFO reset");
      }
    reg (BLKSIZ) = BLOCK;
    reg (BYTCNT) = bytes;
    std::uint32_t flags = R1 | CMD_DAT_EXP | CMD_PRV_DAT_WAIT;
    if (wbuf != nullptr)
      {
        flags |= CMD_DAT_WR;
      }
    if (count > 1)
      {
        flags |= CMD_SEND_STOP; // the controller sends CMD12 (auto-stop)
      }
    emmc::status s = command (idx, arg, flags);
    if (s != emmc::status::ok)
      {
        return s;
      }
    s = (wbuf != nullptr) ? data_write (idx, wbuf, bytes)
                          : data_read (idx, rbuf, bytes);
    if (s != emmc::status::ok)
      {
        return s;
      }
    if (count > 1)
      {
        const std::uint64_t end = clint::now () + ticks (1000);
        while ((reg (RINTSTS) & INT_ACD) == 0)
          {
            if (clint::now () > end)
              {
                return fail (emmc::status::timeout, 12, reg (RINTSTS),
                             "auto-stop not done");
              }
          }
      }
    // A write is complete when the card has programmed it (DAT0 high).
    if (!wait_not_busy (wbuf != nullptr ? 5000 : 1000))
      {
        return fail (emmc::status::timeout, idx, reg (RINTSTS),
                     "busy after transfer");
      }
    return emmc::status::ok;
  }
} // namespace

namespace emmc
{
  const char*
  last_error (void)
  {
    return err;
  }

  status
  init (info& out)
  {
    std::memset (&out, 0, sizeof (out));
    ready = false;
    err[0] = '\0';

    out.verid = reg (VERID);
    fifo = BASE
           + (((out.verid & 0xFFFF) < 0x240A) ? DATA_OFFSET : DATA_240A_OFFSET);

    // The controller: reset, polled (no interrupts, no internal DMA).
    if (!ctrl_reset (CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET))
      {
        return fail (status::error, 0, reg (CTRL), "controller reset");
      }
    reg (RINTSTS) = 0xFFFFFFFFu;
    reg (INTMASK) = 0;
    reg (BMOD) = 0;
    reg (TMOUT) = 0xFFFFFFFFu;
    // SDMMC_SET_FIFOTH(msize 2, rx watermark depth/2 - 1, tx depth/2).
    reg (FIFOTH) = (2u << 28) | ((FIFO_DEPTH / 2 - 1) << 16) | (FIFO_DEPTH / 2);
    reg (CTYPE) = 0; // 1-bit
    reg (PWREN) = 1;
    delay_ms (10);

    if (!set_clock (CLKDIV_IDENT))
      {
        return fail (status::error, 0, reg (CMD), "clock update");
      }
    delay_ms (2); // 74 clocks and more

    // CMD0 GO_IDLE, with the 80 initialisation clocks.
    status s = command (0, 0, R_NONE | CMD_INIT);
    if (s != status::ok)
      {
        return s;
      }

    // CMD1 SEND_OP_COND: sector mode, 2.7-3.6 V and 1.70-1.95 V, until the
    // card is ready (OCR bit 31).
    const std::uint64_t end = clint::now () + ticks (1500);
    for (;;)
      {
        s = command (1, 0x40FF8080u, R3);
        if (s != status::ok)
          {
            return s;
          }
        out.ocr = reg (RESP0);
        if (out.ocr & 0x80000000u)
          {
            break;
          }
        if (clint::now () > end)
          {
            return fail (status::not_ready, 1, out.ocr, "card still busy");
          }
        delay_ms (5);
      }
    if ((out.ocr & 0x40000000u) == 0)
      {
        return fail (status::error, 1, out.ocr, "byte-addressed card");
      }

    // CMD2 ALL_SEND_CID, CMD3 SET_RELATIVE_ADDR, CMD9 SEND_CSD.
    if ((s = command (2, 0, R2)) != status::ok)
      {
        return s;
      }
    out.cid[0] = reg (RESP3);
    out.cid[1] = reg (RESP2);
    out.cid[2] = reg (RESP1);
    out.cid[3] = reg (RESP0);
    if ((s = command (3, RCA << 16, R1)) != status::ok)
      {
        return s;
      }
    if ((s = command (9, RCA << 16, R2)) != status::ok)
      {
        return s;
      }
    out.csd[0] = reg (RESP3);
    out.csd[1] = reg (RESP2);
    out.csd[2] = reg (RESP1);
    out.csd[3] = reg (RESP0);

    // CID: MID [127:120], PNM [103:56].
    {
      const unsigned __int128 v = (static_cast<unsigned __int128> (out.cid[0]) << 96)
                                  | (static_cast<unsigned __int128> (out.cid[1]) << 64)
                                  | (static_cast<unsigned __int128> (out.cid[2]) << 32)
                                  | out.cid[3];
      out.manufacturer = static_cast<std::uint8_t> (v >> 120);
      for (unsigned i = 0; i < 6; ++i)
        {
          const char c = static_cast<char> (v >> (96 - 8 * i));
          out.product[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
        }
      out.product[6] = '\0';
    }

    // CMD7 SELECT_CARD (R1b), then the data clock.
    if ((s = command (7, RCA << 16, R1 | CMD_PRV_DAT_WAIT)) != status::ok)
      {
        return s;
      }
    if (!wait_not_busy (1000))
      {
        return fail (status::timeout, 7, reg (RINTSTS), "busy after select");
      }
    if (!set_clock (CLKDIV_DATA))
      {
        return fail (status::error, 7, reg (CMD), "clock update");
      }

    // CMD8 SEND_EXT_CSD: 512 bytes; SEC_COUNT [215:212], EXT_CSD_REV [192].
    ready = true;
    static std::uint8_t ext[BLOCK];
    if ((s = transfer (8, 0, ext, nullptr, 1)) != status::ok)
      {
        ready = false;
        return s;
      }
    out.sectors = static_cast<std::uint64_t> (ext[212])
                  | (static_cast<std::uint64_t> (ext[213]) << 8)
                  | (static_cast<std::uint64_t> (ext[214]) << 16)
                  | (static_cast<std::uint64_t> (ext[215]) << 24);
    out.ext_csd_rev = ext[192];
    return status::ok;
  }

  status
  read (std::uint64_t lba, void* buf, std::uint32_t count)
  {
    if (!ready)
      {
        return status::no_init;
      }
    if (count == 0)
      {
        return status::ok;
      }
    return transfer (count > 1 ? 18 : 17, static_cast<std::uint32_t> (lba),
                     static_cast<std::uint8_t*> (buf), nullptr, count);
  }

  status
  write (std::uint64_t lba, const void* buf, std::uint32_t count)
  {
    if (!ready)
      {
        return status::no_init;
      }
    if (count == 0)
      {
        return status::ok;
      }
    return transfer (count > 1 ? 25 : 24, static_cast<std::uint32_t> (lba),
                     nullptr, static_cast<const std::uint8_t*> (buf), count);
  }
} // namespace emmc
