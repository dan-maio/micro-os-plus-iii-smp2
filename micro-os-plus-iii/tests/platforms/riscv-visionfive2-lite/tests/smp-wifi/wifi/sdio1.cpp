/*
 * sdio1.cpp -- the VisionFive 2 Lite's SDIO1 host, polled (sdio1.hpp).
 *
 * The controller half is the board's eMMC driver (src/emmc.cpp) pointed at
 * mmc1; register names and bits are those of Linux's dw_mmc.h.  What is new
 * here is everything U-Boot does not do for this controller -- its clocks,
 * its reset, its pins, the module's power pin -- and the SDIO card protocol.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "sdio1.hpp"

#include <clint.hpp>
#include <riscv-board.hpp>

#include <cstdio>
#include <cstring>

namespace
{
  // ---------------------------------------------------------------- SoC
  constexpr std::uintptr_t BASE = 0x16020000u;    // mmc1
  constexpr std::uintptr_t SYSCRG = 0x13020000u;
  constexpr std::uintptr_t SYSGPIO = 0x13040000u; // sys_iomux

  // starfive,jh7110-crg.h
  constexpr unsigned SYSCLK_SDIO1_AHB = 92;
  constexpr unsigned SYSCLK_SDIO1_SDCARD = 94;
  constexpr unsigned SYSRST_SDIO1_AHB = 65;
  constexpr std::uint32_t CLK_ENABLE = 1u << 31;
  constexpr std::uint32_t CLK_DIV_MASK = 0x00FFFFFFu;
  // clk-starfive-jh7110-sys.c: the SYSCRG resets are 4 words of assert bits
  // at 0x2F8 and 4 words of status at 0x308 (status bit set = released).
  constexpr std::uintptr_t RST_ASSERT = SYSCRG + 0x2F8u;
  constexpr std::uintptr_t RST_STATUS = SYSCRG + 0x308u;

  // sdio1_sdcard is a gate + divider (max 15) of axi_cfg0 = bus_root / 3.
  // Measured for sdio0 on this board (include/emmc.hpp): PLL2 1188 MHz / 3 =
  // 396 MHz.  Divider 8 gives the 49.5 MHz Linux asks for
  // (assigned-clock-rates = <50000000>).
  constexpr std::uint32_t AXI_CFG0_HZ = 396000000u;
  constexpr std::uint32_t CIU_DIV = 8;
  constexpr std::uint32_t CIU_HZ = AXI_CFG0_HZ / CIU_DIV;
  // Card clock = ciu / (2 * CLKDIV).
  constexpr std::uint32_t CLKDIV_IDENT = 62; // 399 kHz
  constexpr std::uint32_t CLKDIV_DATA = 1;   // 24.75 MHz (default speed)

  // jh7110-pinfunc.h / pinctrl-starfive-jh7110-sys.c: per GPIO one byte lane
  // of DOEN (0x000, 6 bits), DOUT (0x040, 7 bits); per input function one
  // byte lane of GPI (0x080, 7 bits, value = GPIO + 2); pad configuration
  // one word per GPIO at 0x120.
  constexpr std::uintptr_t IOMUX_DOEN = SYSGPIO + 0x000u;
  constexpr std::uintptr_t IOMUX_DOUT = SYSGPIO + 0x040u;
  constexpr std::uintptr_t IOMUX_GPI = SYSGPIO + 0x080u;
  constexpr std::uintptr_t IOMUX_PAD = SYSGPIO + 0x120u;
  constexpr std::uint32_t PAD_IE = 1u << 0;
  constexpr std::uint32_t PAD_DS_12MA = 3u << 1;
  constexpr std::uint32_t PAD_PU = 1u << 3;
  constexpr std::uint32_t PAD_SMT = 1u << 6;
  constexpr unsigned GPOUT_LOW = 0, GPOUT_HIGH = 1;
  constexpr unsigned GPOEN_ENABLE = 0;
  constexpr unsigned GPI_NONE = 0xFF;

  struct pin
  {
    unsigned gpio, dout, doen, gpi;
  };
  // GPOUT_SYS_SDIO1_CLK 55, _CMD 57, _DATA0..3 58..61;
  // GPOEN_SYS_SDIO1_CMD 19, _DATA0..3 20..23;
  // GPI_SYS_SDIO1_CMD 44, _DATA0..3 45..48.
  constexpr pin PIN_CLK{ 10, 55, GPOEN_ENABLE, GPI_NONE };
  constexpr pin PINS_IO[] = {
    { 9, 57, 19, 44 },  // CMD
    { 11, 58, 20, 45 }, // DATA0
    { 12, 59, 21, 46 }, // DATA1
    { 7, 60, 22, 47 },  // DATA2
    { 8, 61, 23, 48 },  // DATA3
  };
  constexpr unsigned GPIO_WL_REG_ON = 33;

  // ------------------------------------------------------------ dw_mmc.h
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
  constexpr std::uintptr_t RINTSTS = 0x044;
  constexpr std::uintptr_t STATUS = 0x048;
  constexpr std::uintptr_t FIFOTH = 0x04c;
  constexpr std::uintptr_t VERID = 0x06c;
  constexpr std::uintptr_t BMOD = 0x080;
  constexpr std::uintptr_t DATA_OFFSET = 0x100;      // VERID < 2.40a
  constexpr std::uintptr_t DATA_240A_OFFSET = 0x200; // VERID >= 2.40a

  constexpr std::uint32_t CTRL_RESET = 1u << 0;
  constexpr std::uint32_t CTRL_FIFO_RESET = 1u << 1;
  constexpr std::uint32_t CTRL_DMA_RESET = 1u << 2;
  constexpr std::uint32_t CLKEN_ENABLE = 1u << 0;
  constexpr std::uint32_t INT_EBE = 1u << 15;
  constexpr std::uint32_t INT_SBE = 1u << 13;
  constexpr std::uint32_t INT_HLE = 1u << 12;
  constexpr std::uint32_t INT_FRUN = 1u << 11;
  constexpr std::uint32_t INT_DRTO = 1u << 9;
  constexpr std::uint32_t INT_RTO = 1u << 8;
  constexpr std::uint32_t INT_DCRC = 1u << 7;
  constexpr std::uint32_t INT_RCRC = 1u << 6;
  constexpr std::uint32_t INT_DATA_OVER = 1u << 3;
  constexpr std::uint32_t INT_CMD_DONE = 1u << 2;
  constexpr std::uint32_t INT_RESP_ERR = 1u << 1;
  constexpr std::uint32_t INT_DATA_ERRORS
      = INT_EBE | INT_SBE | INT_HLE | INT_FRUN | INT_DRTO | INT_DCRC;
  constexpr std::uint32_t CMD_START = 1u << 31;
  constexpr std::uint32_t CMD_USE_HOLD = 1u << 29;
  constexpr std::uint32_t CMD_UPD_CLK = 1u << 21;
  constexpr std::uint32_t CMD_INIT = 1u << 15;
  constexpr std::uint32_t CMD_PRV_DAT_WAIT = 1u << 13;
  constexpr std::uint32_t CMD_DAT_WR = 1u << 10;
  constexpr std::uint32_t CMD_DAT_EXP = 1u << 9;
  constexpr std::uint32_t CMD_RESP_CRC = 1u << 8;
  constexpr std::uint32_t CMD_RESP_EXP = 1u << 6;
  constexpr std::uint32_t STATUS_BUSY = 1u << 9;

  constexpr std::uint32_t R_NONE = 0;
  constexpr std::uint32_t R_CRC = CMD_RESP_EXP | CMD_RESP_CRC; // R1, R5, R6
  constexpr std::uint32_t R4 = CMD_RESP_EXP;                   // OCR: no CRC

  constexpr std::uint32_t FIFO_DEPTH = 32; // jh7110.dtsi fifo-depth

  // ------------------------------------------------------------ SDIO
  constexpr std::uint32_t CCCR_REV = 0x00;
  constexpr std::uint32_t CCCR_IO_ENABLE = 0x02;
  constexpr std::uint32_t CCCR_IO_READY = 0x03;
  constexpr std::uint32_t CCCR_ABORT = 0x06;
  constexpr std::uint32_t CCCR_BUS_IF = 0x07;
  constexpr std::uint32_t CCCR_CAPS = 0x08;
  constexpr std::uint32_t CCCR_CIS = 0x09;
  constexpr std::uint32_t FBR_SIZE = 0x100;
  constexpr std::uint32_t FBR_CIS = 0x09;
  constexpr std::uint32_t FBR_BLKSIZE = 0x10;
  // R5 flags (RESP0 bits 15..8): COM_CRC, ILLEGAL, ERROR, FUNCTION, RANGE.
  constexpr std::uint32_t R5_ERRORS = 0xCB00u;

  std::uintptr_t fifo = BASE + DATA_240A_OFFSET;
  bool ready = false;
  char err[112] = "";

  volatile std::uint32_t&
  mmio (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  volatile std::uint32_t&
  reg (std::uintptr_t off)
  {
    return mmio (BASE + off);
  }

  std::uint64_t
  ticks_us (std::uint64_t us)
  {
    return us * (riscv_board::TIMEBASE_HZ / 1000000u);
  }

  void
  delay_ms (std::uint32_t ms)
  {
    sdio1::delay_us (ms * 1000u);
  }

  bool
  fail (unsigned cmd, std::uint32_t rint, const char* what)
  {
    std::snprintf (err, sizeof (err), "CMD%u: %s (RINTSTS 0x%08x)", cmd, what,
                   static_cast<unsigned> (rint));
    return false;
  }

  // ------------------------------------------------------------ pins
  void
  lane_write (std::uintptr_t base, unsigned index, std::uint32_t mask,
              std::uint32_t value)
  {
    const std::uintptr_t a = base + 4u * (index / 4u);
    const unsigned shift = 8u * (index % 4u);
    mmio (a) = (mmio (a) & ~(mask << shift)) | ((value & mask) << shift);
  }

  void
  mux (const pin& p, std::uint32_t pad)
  {
    lane_write (IOMUX_DOUT, p.gpio, 0x7Fu, p.dout);
    lane_write (IOMUX_DOEN, p.gpio, 0x3Fu, p.doen);
    if (p.gpi != GPI_NONE)
      {
        lane_write (IOMUX_GPI, p.gpi, 0x7Fu, p.gpio + 2u);
      }
    mmio (IOMUX_PAD + 4u * p.gpio) = pad;
  }

  void
  reg_on (bool high)
  {
    lane_write (IOMUX_DOUT, GPIO_WL_REG_ON, 0x7Fu, high ? GPOUT_HIGH : GPOUT_LOW);
    lane_write (IOMUX_DOEN, GPIO_WL_REG_ON, 0x3Fu, GPOEN_ENABLE);
  }

  // ------------------------------------------------------------ controller
  bool
  ctrl_reset (std::uint32_t bits)
  {
    reg (CTRL) = reg (CTRL) | bits;
    const std::uint64_t end = clint::now () + ticks_us (500000);
    while (reg (CTRL) & bits)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
    return true;
  }

  bool
  ciu_update (void)
  {
    reg (CMDARG) = 0;
    reg (CMD) = CMD_START | CMD_UPD_CLK | CMD_PRV_DAT_WAIT;
    const std::uint64_t end = clint::now () + ticks_us (500000);
    while (reg (CMD) & CMD_START)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
    return true;
  }

  // dw_mci_setup_bus(): CLKENA 0, update, CLKDIV, update, CLKENA, update.
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

  bool
  wait_not_busy (std::uint32_t ms)
  {
    const std::uint64_t end = clint::now () + ticks_us (ms * 1000ull);
    while (reg (STATUS) & STATUS_BUSY)
      {
        if (clint::now () > end)
          {
            return false;
          }
      }
    return true;
  }

  bool
  command (unsigned idx, std::uint32_t arg, std::uint32_t flags)
  {
    reg (RINTSTS) = 0xFFFFFFFFu;
    reg (CMDARG) = arg;
    __asm__ volatile("fence" ::: "memory");
    reg (CMD) = CMD_START | CMD_USE_HOLD | flags | (idx & 0x3Fu);

    const std::uint64_t end = clint::now () + ticks_us (1000000);
    std::uint32_t r;
    while (((r = reg (RINTSTS)) & INT_CMD_DONE) == 0)
      {
        if (clint::now () > end)
          {
            return fail (idx, r, "no CMD_DONE");
          }
      }
    if (r & INT_RTO)
      {
        return fail (idx, r, "response timeout");
      }
    if ((flags & CMD_RESP_CRC) && (r & INT_RCRC))
      {
        return fail (idx, r, "response CRC");
      }
    if (r & INT_RESP_ERR)
      {
        return fail (idx, r, "response error");
      }
    return true;
  }

  volatile std::uint32_t&
  fifo32 (void)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (fifo);
  }

  bool
  data_read (std::uint8_t* buf, std::uint32_t bytes)
  {
    std::uint32_t done = 0;
    const std::uint64_t end = clint::now () + ticks_us (2000000);
    for (;;)
      {
        const std::uint32_t r = reg (RINTSTS);
        if (r & INT_DATA_ERRORS)
          {
            return fail (53, r, (r & INT_DCRC) ? "read data CRC"
                                               : "read data error");
          }
        std::uint32_t n = (reg (STATUS) >> 17) & 0x1FFFu; // FIFO entries
        while (n-- && done < bytes)
          {
            const std::uint32_t v = fifo32 ();
            std::memcpy (buf + done, &v, 4);
            done += 4;
          }
        if ((r & INT_DATA_OVER) && done >= bytes)
          {
            return true;
          }
        if (clint::now () > end)
          {
            return fail (53, r, "read data timeout");
          }
      }
  }

  bool
  data_write (const std::uint8_t* buf, std::uint32_t bytes)
  {
    std::uint32_t done = 0;
    const std::uint64_t end = clint::now () + ticks_us (2000000);
    for (;;)
      {
        const std::uint32_t r = reg (RINTSTS);
        if (r & INT_DATA_ERRORS)
          {
            return fail (53, r, (r & INT_DCRC) ? "write data CRC"
                                               : "write data error");
          }
        const std::uint32_t used = (reg (STATUS) >> 17) & 0x1FFFu;
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
            return (done >= bytes) ? true : fail (53, r, "short write");
          }
        if (clint::now () > end)
          {
            return fail (53, r, "write data timeout");
          }
      }
  }

  // CMD52 on the wire, whatever the state of `ready`.
  bool
  cmd52 (bool write, unsigned fn, std::uint32_t addr, std::uint8_t in,
         std::uint8_t* out)
  {
    const std::uint32_t arg = (write ? 0x80000000u : 0u) | ((fn & 7u) << 28)
                              | ((addr & 0x1FFFFu) << 9) | in;
    if (!command (52, arg, R_CRC))
      {
        return false;
      }
    const std::uint32_t r5 = reg (RESP0);
    if (r5 & R5_ERRORS)
      {
        return fail (52, r5, "R5 error flags (value is RESP0)");
      }
    if (out != nullptr)
      {
        *out = static_cast<std::uint8_t> (r5);
      }
    return true;
  }

  bool
  cmd53 (bool write, unsigned fn, std::uint32_t addr, std::uint8_t* rbuf,
         const std::uint8_t* wbuf, std::uint32_t bytes)
  {
    if (!ready)
      {
        return fail (53, 0, "no card");
      }
    if (bytes == 0 || (bytes % sdio1::block_size) != 0
        || bytes / sdio1::block_size > 511u)
      {
        return fail (53, bytes, "length is not 1..511 blocks (value is bytes)");
      }
    if (!wait_not_busy (2000))
      {
        return fail (53, reg (RINTSTS), "card busy");
      }
    if (!ctrl_reset (CTRL_FIFO_RESET))
      {
        return fail (53, reg (CTRL), "FIFO reset");
      }
    reg (BLKSIZ) = sdio1::block_size;
    reg (BYTCNT) = bytes;
    // Block mode (bit 27), fixed address (bit 26 = 0): a FIFO register.
    const std::uint32_t arg = (write ? 0x80000000u : 0u) | ((fn & 7u) << 28)
                              | (1u << 27) | ((addr & 0x1FFFFu) << 9)
                              | (bytes / sdio1::block_size);
    std::uint32_t flags = R_CRC | CMD_DAT_EXP | CMD_PRV_DAT_WAIT;
    if (write)
      {
        flags |= CMD_DAT_WR;
      }
    if (!command (53, arg, flags))
      {
        return false;
      }
    const std::uint32_t r5 = reg (RESP0);
    if (r5 & R5_ERRORS)
      {
        (void)ctrl_reset (CTRL_FIFO_RESET);
        return fail (53, r5, "R5 error flags (value is RESP0)");
      }
    if (!(write ? data_write (wbuf, bytes) : data_read (rbuf, bytes)))
      {
        (void)ctrl_reset (CTRL_FIFO_RESET);
        return false;
      }
    if (write && !wait_not_busy (2000))
      {
        return fail (53, reg (RINTSTS), "busy after write");
      }
    return true;
  }

  // The first CISTPL_MANFID (0x20) of the CIS at `ptr`.
  bool
  cis_manfid (std::uint32_t ptr, std::uint16_t& vendor, std::uint16_t& device)
  {
    for (unsigned guard = 0; guard < 64u && ptr != 0; ++guard)
      {
        std::uint8_t code = 0xFF, link = 0;
        if (!cmd52 (false, 0, ptr, 0, &code) || code == 0xFF)
          {
            return false;
          }
        if (!cmd52 (false, 0, ptr + 1, 0, &link) || link == 0xFF)
          {
            return false;
          }
        if (code == 0x20 && link >= 4)
          {
            std::uint8_t b[4] = {};
            for (unsigned i = 0; i < 4u; ++i)
              {
                if (!cmd52 (false, 0, ptr + 2 + i, 0, &b[i]))
                  {
                    return false;
                  }
              }
            vendor = static_cast<std::uint16_t> (b[0] | (b[1] << 8));
            device = static_cast<std::uint16_t> (b[2] | (b[3] << 8));
            return true;
          }
        ptr += 2u + link;
      }
    return false;
  }

  bool
  cis_pointer (std::uint32_t at, std::uint32_t& ptr)
  {
    std::uint8_t b[3] = {};
    for (unsigned i = 0; i < 3u; ++i)
      {
        if (!cmd52 (false, 0, at + i, 0, &b[i]))
          {
            return false;
          }
      }
    ptr = b[0] | (b[1] << 8) | (static_cast<std::uint32_t> (b[2]) << 16);
    return true;
  }

  // Power the module with WL_REG_ON at `high`, then ask who is there (CMD5).
  bool
  wake (bool high, std::uint32_t& ocr)
  {
    reg_on (!high);
    delay_ms (50);
    reg_on (high);
    delay_ms (200);

    // 74+ clocks with CMD0 (the card may also be an SD combo), then CMD5.
    (void)command (0, 0, R_NONE | CMD_INIT);
    delay_ms (2);
    for (unsigned attempt = 0; attempt < 3u; ++attempt)
      {
        if (command (5, 0, R4))
          {
            ocr = reg (RESP0);
            return true;
          }
        delay_ms (20);
      }
    return false;
  }
} // namespace

namespace sdio1
{
  void
  delay_us (std::uint32_t us)
  {
    const std::uint64_t end = clint::now () + ticks_us (us);
    while (clint::now () < end)
      {
      }
  }

  const char*
  last_error (void)
  {
    return err;
  }

  bool
  init (info& out)
  {
    out = {};
    ready = false;
    err[0] = '\0';

    // 1. Clocks and reset.  U-Boot leaves mmc1 alone unless it used it.
    mmio (SYSCRG + 4u * SYSCLK_SDIO1_AHB) |= CLK_ENABLE;
    mmio (SYSCRG + 4u * SYSCLK_SDIO1_SDCARD)
        = (mmio (SYSCRG + 4u * SYSCLK_SDIO1_SDCARD) & ~CLK_DIV_MASK)
          | CLK_ENABLE | CIU_DIV;
    out.clk_ahb = mmio (SYSCRG + 4u * SYSCLK_SDIO1_AHB);
    out.clk_card = mmio (SYSCRG + 4u * SYSCLK_SDIO1_SDCARD);
    {
      const std::uintptr_t word = 4u * (SYSRST_SDIO1_AHB / 32u);
      const std::uint32_t bit = 1u << (SYSRST_SDIO1_AHB % 32u);
      mmio (RST_ASSERT + word) |= bit;
      delay_us (100);
      mmio (RST_ASSERT + word) &= ~bit;
      const std::uint64_t end = clint::now () + ticks_us (100000);
      while ((mmio (RST_STATUS + word) & bit) == 0)
        {
          if (clint::now () > end)
            {
              return fail (0, mmio (RST_STATUS + word),
                           "SDIO1 reset not released (value is the status "
                           "word)");
            }
        }
    }

    // 2. Pins: CLK out; CMD and DATA0..3 bidirectional with pull-ups.
    mux (PIN_CLK, PAD_PU | PAD_DS_12MA);
    for (const pin& p : PINS_IO)
      {
        mux (p, PAD_IE | PAD_DS_12MA | PAD_PU | PAD_SMT);
      }

    // 3. The controller: reset, polled (no interrupts, no internal DMA).
    out.verid = reg (VERID);
    if ((out.verid >> 16) != 0x5342u)
      {
        return fail (0, out.verid,
                     "mmc1 does not answer (value is VERID; clock/reset?)");
      }
    fifo = BASE
           + (((out.verid & 0xFFFFu) < 0x240Au) ? DATA_OFFSET
                                                : DATA_240A_OFFSET);
    if (!ctrl_reset (CTRL_RESET | CTRL_FIFO_RESET | CTRL_DMA_RESET))
      {
        return fail (0, reg (CTRL), "controller reset");
      }
    reg (RINTSTS) = 0xFFFFFFFFu;
    reg (INTMASK) = 0;
    reg (BMOD) = 0;
    reg (TMOUT) = 0xFFFFFFFFu;
    reg (FIFOTH) = (2u << 28) | ((FIFO_DEPTH / 2 - 1) << 16) | (FIFO_DEPTH / 2);
    reg (CTYPE) = 0; // 1-bit
    reg (PWREN) = 1;
    delay_ms (10);
    if (!set_clock (CLKDIV_IDENT))
      {
        return fail (0, reg (CMD), "clock update");
      }
    delay_ms (2);

    // 4. Module power.  The vendor device tree names the pin (GPIO33) and
    //    flags it active low; the module's WL_REG_ON is active high.  Which
    //    of the two holds on this board is decided by who answers CMD5.
    std::uint32_t ocr = 0;
    out.reg_on_high = true;
    if (!wake (true, ocr))
      {
        out.reg_on_high = false;
        if (!wake (false, ocr))
          {
            return fail (5, reg (RINTSTS),
                         "no SDIO card with WL_REG_ON (GPIO33) high or low");
          }
      }

    // 5. CMD5 with the 3.2-3.4 V window until the card is ready (bit 31).
    {
      const std::uint64_t end = clint::now () + ticks_us (1000000);
      for (;;)
        {
          if (!command (5, 0x00300000u, R4))
            {
              return false;
            }
          ocr = reg (RESP0);
          if (ocr & 0x80000000u)
            {
              break;
            }
          if (clint::now () > end)
            {
              return fail (5, ocr, "card never ready (value is the OCR)");
            }
          delay_ms (10);
        }
    }
    out.ocr = ocr;
    out.functions = static_cast<std::uint8_t> ((ocr >> 28) & 7u);

    // 6. CMD3 (the card publishes its RCA), CMD7 (select).
    if (!command (3, 0, R_CRC))
      {
        return false;
      }
    out.rca = static_cast<std::uint16_t> (reg (RESP0) >> 16);
    if (!command (7, static_cast<std::uint32_t> (out.rca) << 16,
                  R_CRC | CMD_PRV_DAT_WAIT))
      {
        return false;
      }

    // 7. CCCR: revision, capabilities, the IDs in the CIS.
    if (!cmd52 (false, 0, CCCR_REV, 0, &out.cccr_rev)
        || !cmd52 (false, 0, CCCR_CAPS, 0, &out.card_caps))
      {
        return false;
      }
    {
      std::uint32_t ptr = 0;
      if (cis_pointer (CCCR_CIS, ptr))
        {
          (void)cis_manfid (ptr, out.vendor, out.device);
        }
      if (out.functions >= 1 && cis_pointer (FBR_SIZE + FBR_CIS, ptr))
        {
          (void)cis_manfid (ptr, out.f1_vendor, out.f1_device);
        }
      err[0] = '\0'; // a missing tuple is not a failure
    }

    // 8. 4-bit bus (CCCR bus interface: width 10b, card-detect pull-up off),
    //    then the data clock.
    {
      std::uint8_t bus = 0;
      if (!cmd52 (false, 0, CCCR_BUS_IF, 0, &bus)
          || !cmd52 (true, 0, CCCR_BUS_IF,
                     static_cast<std::uint8_t> ((bus & ~0x03u) | 0x82u),
                     nullptr))
        {
          return false;
        }
      reg (CTYPE) = 1; // 4-bit
    }
    if (!set_clock (CLKDIV_DATA))
      {
        return fail (0, reg (CMD), "clock update");
      }
    out.card_hz = CIU_HZ / (2u * CLKDIV_DATA);
    ready = true;
    return true;
  }

  bool
  read_byte (unsigned fn, std::uint32_t addr, std::uint8_t& value)
  {
    return ready ? cmd52 (false, fn, addr, 0, &value) : fail (52, 0, "no card");
  }

  bool
  write_byte (unsigned fn, std::uint32_t addr, std::uint8_t value)
  {
    return ready ? cmd52 (true, fn, addr, value, nullptr)
                 : fail (52, 0, "no card");
  }

  bool
  read_fifo (unsigned fn, std::uint32_t addr, std::uint8_t* buf,
             std::uint32_t bytes)
  {
    return cmd53 (false, fn, addr, buf, nullptr, bytes);
  }

  bool
  write_fifo (unsigned fn, std::uint32_t addr, const std::uint8_t* buf,
              std::uint32_t bytes)
  {
    return cmd53 (true, fn, addr, nullptr, buf, bytes);
  }

  bool
  set_block_size (unsigned fn, std::uint16_t size)
  {
    const std::uint32_t at = FBR_SIZE * fn + FBR_BLKSIZE;
    return write_byte (0, at, static_cast<std::uint8_t> (size))
           && write_byte (0, at + 1, static_cast<std::uint8_t> (size >> 8));
  }

  bool
  enable_function (unsigned fn)
  {
    std::uint8_t v = 0;
    if (!read_byte (0, CCCR_IO_ENABLE, v)
        || !write_byte (0, CCCR_IO_ENABLE,
                        static_cast<std::uint8_t> (v | (1u << fn))))
      {
        return false;
      }
    const std::uint64_t end = clint::now () + ticks_us (1000000);
    for (;;)
      {
        if (!read_byte (0, CCCR_IO_READY, v))
          {
            return false;
          }
        if (v & (1u << fn))
          {
            return true;
          }
        if (clint::now () > end)
          {
            return fail (52, v, "function never ready (value is IORx)");
          }
        delay_ms (1);
      }
  }
} // namespace sdio1
