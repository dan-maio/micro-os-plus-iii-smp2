/*
 * gmac.cpp -- VisionFive 2 Lite gigabit Ethernet (include/gmac.hpp).
 *
 * DesignWare GMAC 5.20 at 0x16030000, polled, one DMA channel.  The frame
 * rings live in .bss and are handed to the DMA as physical addresses (the
 * port runs the VF2 without the MMU).  JH-7110 DMA is not cache coherent:
 * every descriptor and buffer is written back / invalidated through the L2
 * controller's FLUSH64 (vf2_dcache_flush(), the same helper the USB driver
 * uses), exactly as Linux's sifive_ccache.c does.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <gmac.hpp>

#include <clint.hpp>

// The JH-7110's L2 cache controller (sifive_ccache.c): write back and
// invalidate a range, line by line.  Defined in the board's USB support,
// which this test links for the CDC console.
extern "C" void vf2_dcache_flush (const void* addr, std::uint32_t len);

#include <cstdio>
#include <cstring>

namespace
{
  using u32 = std::uint32_t;

  constexpr std::uintptr_t BASE = gmac::base;

  // MAC (dwmac4.h).
  constexpr std::uintptr_t MAC_CONFIG = 0x000;
  constexpr std::uintptr_t MAC_PACKET_FILTER = 0x008;
  constexpr std::uintptr_t MAC_VERSION = 0x110;
  constexpr std::uintptr_t MAC_MDIO_ADDR = 0x200;
  constexpr std::uintptr_t MAC_MDIO_DATA = 0x204;
  constexpr std::uintptr_t MAC_ADDR_HIGH = 0x300;
  constexpr std::uintptr_t MAC_ADDR_LOW = 0x304;
  constexpr std::uintptr_t MAC_ONEUS_TIC = 0x0dc;

  constexpr u32 MAC_CONFIG_RE = 1u << 0;
  constexpr u32 MAC_CONFIG_TE = 1u << 1;
  constexpr u32 MAC_CONFIG_DM = 1u << 13;
  constexpr u32 MAC_CONFIG_FES = 1u << 14;
  constexpr u32 MAC_CONFIG_PS = 1u << 15;
  constexpr u32 MAC_CONFIG_JD = 1u << 17;
  // Linux's "default operating mode of the MAC" (dwmac4.h GMAC_CORE_INIT):
  // port select (RGMII), packet burst enable, disable carrier sense during
  // transmission, jabber enable+disable.  Leaving these clear is not a
  // harmless difference: PS selects the RGMII port of the MAC.
  constexpr u32 MAC_CONFIG_BE = 1u << 18;
  constexpr u32 MAC_CONFIG_DCRS = 1u << 9;
  constexpr u32 MAC_CONFIG_JE = 1u << 16;
  // Measured on this board: PS (port select) must stay CLEAR -- setting it
  // as Linux's GMAC_CORE_INIT does stops traffic completely.
  constexpr u32 MAC_CORE_INIT = MAC_CONFIG_JD;

  constexpr u32 MAC_FILTER_RA = 1u << 31; // receive all
  constexpr u32 MAC_FILTER_PM = 1u << 4;  // pass all multicast

  // MDIO (stmmac_mdio.c, GMAC4 layout).
  constexpr u32 MDIO_ADDR_GBUSY = 1u << 0;
  constexpr u32 MDIO_ADDR_GOC_WRITE = 1u << 2;
  constexpr u32 MDIO_ADDR_GOC_READ = 3u << 2;
  // GMAC4/GMAC5 layout (Linux dwmac4_core.c: mii.addr_mask GENMASK(25,21),
  // mii.reg_mask GENMASK(20,16), mii.clk_csr_mask GENMASK(11,8)): PA in
  // 25:21, GR in 20:16, the CSR-clock divider in 11:8.
  constexpr u32 MDIO_ADDR_PA_SHIFT = 21;
  constexpr u32 MDIO_ADDR_GR_SHIFT = 16;
  constexpr u32 MDIO_ADDR_CR_SHIFT = 8;

  // MTL (dwmac4.h): transmit and receive queue 0 operation modes.
  constexpr std::uintptr_t MTL_TXQ0_OP_MODE = 0x0d00;
  constexpr std::uintptr_t MTL_RXQ0_OP_MODE = 0x0d30;
  constexpr u32 MTL_TX_TSF = 1u << 1;
  constexpr u32 MTL_RX_RSF = 1u << 5;
  // Both queue operation-mode registers have an enable bit of their own
  // (MTL_OP_MODE_TXQEN / RXQEN): clearing it disables the queue, which is
  // exactly what the first version did by writing only the store-and-forward
  // bit.
  constexpr u32 MTL_OP_TXQEN = 1u << 3;
  constexpr u32 MTL_OP_RXQEN = 1u << 3;

  // DMA (dwmac4_dma.h).
  constexpr std::uintptr_t DMA_BUS_MODE = 0x1000;
  constexpr std::uintptr_t DMA_SYS_BUS_MODE = 0x1004;
  constexpr std::uintptr_t DMA_STATUS = 0x1008;
  constexpr std::uintptr_t DMA_CH0_CTRL = 0x1100;
  constexpr std::uintptr_t DMA_CH0_TX_CTRL = 0x1104;
  constexpr std::uintptr_t DMA_CH0_RX_CTRL = 0x1108;
  constexpr std::uintptr_t DMA_CH0_TX_BASE_HI = 0x1110;
  constexpr std::uintptr_t DMA_CH0_TX_BASE = 0x1114;
  constexpr std::uintptr_t DMA_CH0_RX_BASE_HI = 0x1118;
  constexpr std::uintptr_t DMA_CH0_RX_BASE = 0x111c;
  constexpr std::uintptr_t DMA_CH0_TX_END = 0x1120;
  constexpr std::uintptr_t DMA_CH0_RX_END = 0x1128;
  constexpr std::uintptr_t DMA_CH0_TX_RING_LEN = 0x112c;
  constexpr std::uintptr_t DMA_CH0_RX_RING_LEN = 0x1130;
  constexpr std::uintptr_t DMA_CH0_INTR_ENA = 0x1134;
  constexpr std::uintptr_t DMA_CH0_RX_WD = 0x1138;

  constexpr u32 DMA_BUS_SFT_RESET = 1u << 0;
  constexpr u32 DMA_SYS_FB = 1u << 0;   // fixed burst (jh7110: snps,fixed-burst)
  constexpr u32 DMA_SYS_MB = 1u << 14;
  constexpr u32 DMA_CH_CTRL_PBLX8 = 1u << 16;
  constexpr u32 DMA_CH_PBL_SHIFT = 16; // snps,txpbl / snps,rxpbl = 16
  constexpr u32 DMA_CH_TX_ST = 1u << 0;
  constexpr u32 DMA_CH_RX_ST = 1u << 0;

  // Descriptors (dwmac4_descs.h): four words, 16 bytes each.
  struct desc
  {
    u32 w0; // buffer 1 address (low)
    u32 w1; // buffer 2 address / buffer 1 address high
    u32 w2; // buffer 2 address high / buffer 1 size
    u32 w3; // OWN + control (read format), status (write back)
  };
  static_assert (sizeof (desc) == 16, "DW MAC 4/5 descriptor is 16 bytes");

  constexpr u32 DESC_OWN = 1u << 31;
  constexpr u32 DESC_IOCO = 1u << 30;   // interrupt on completion (unused)
  constexpr u32 DESC_BUF1V = 1u << 24;  // RX: buffer 1 valid address
  constexpr u32 DESC_FD = 1u << 29;     // TX: first descriptor
  constexpr u32 DESC_LD = 1u << 28;     // TX: last descriptor
  constexpr u32 DESC_SIZE_MASK = 0x3fffu;
  constexpr u32 DESC_ERR_SUMMARY = 1u << 15;
  constexpr u32 DESC_RX_CRC = 1u << 24;
  constexpr u32 DESC_RX_OVF = 1u << 21;

  constexpr unsigned RX_COUNT = 16;
  constexpr unsigned TX_COUNT = 8;
  constexpr std::size_t RX_BUFFER = 1600;
  constexpr std::size_t TX_BUFFER = 1600;

  alignas (64) desc rx_ring[RX_COUNT];
  alignas (64) desc tx_ring[TX_COUNT];
  alignas (64) std::uint8_t rx_buf[RX_COUNT][RX_BUFFER];
  alignas (64) std::uint8_t tx_buf[TX_COUNT][TX_BUFFER];

  unsigned rx_next = 0;
  unsigned tx_next = 0;

  gmac::info st{};
  gmac::counters cnt{};
  char err[96] = "ok";
  bool diag_dma_ok = true;
  gmac::trace_fn trace_hook = nullptr;

  void
  trace (const char* text)
  {
    if (trace_hook != nullptr)
      {
        trace_hook (text);
      }
  }

  std::uint32_t bus_dump[2][2] = {{0, 0}, {0, 0}};

  unsigned tx_mux = 0;
  bool tx_mux_mdio[2] = {false, false};
  bool tx_mux_ok[2] = {false, false};   // DMA reset cleared

  inline volatile u32&
  r (std::uintptr_t off)
  {
    return *reinterpret_cast<volatile u32*> (BASE + off);
  }

  std::uint64_t
  now (void)
  {
    return clint::now ();
  }

  // mtime is the JH-7110's 4 MHz timebase (riscv-board.hpp TIMEBASE_HZ).
  constexpr std::uint64_t
  ticks (std::uint32_t ms)
  {
    return (static_cast<std::uint64_t> (ms) * riscv_board::TIMEBASE_HZ) / 1000u;
  }

  bool
  wait_ms (std::uint32_t ms, bool (*done) ())
  {
    const std::uint64_t end = now () + ticks (ms);
    while (now () < end)
      {
        if (done ())
          {
            return true;
          }
      }
    return done ();
  }

  void
  set_error (const char* what)
  {
    std::snprintf (err, sizeof (err), "%s", what);
  }

  // ---------------------------------------------------------------- MDIO

  bool
  mdio_wait (void)
  {
    for (unsigned i = 0; i < 100000u; ++i)
      {
        if ((r (MAC_MDIO_ADDR) & MDIO_ADDR_GBUSY) == 0)
          {
            return true;
          }
      }
    return false;
  }

  // MDC divider (MDIO_ADDR bits 11:8).  The CSR clock here is the AXI-side
  // "stmmaceth" clock; the CSR range 4 (STMMAC_CSR_100_150M, /62) keeps MDC
  // well under the 2.5 MHz the standard asks for even if that clock runs at
  // the top of the range.  start() sweeps the field anyway: a wrong MDC rate
  // is otherwise indistinguishable from a dead PHY.
  constinit u32 mdio_csr = 4u;

  // The last frame's raw registers, for the bring-up log.
  constinit u32 mdio_last_addr = 0;
  constinit u32 mdio_last_data = 0;

  // What FES/DM currently hold, so a link that resolves later can be applied.
  constinit unsigned applied_speed = 0;
  constinit bool applied_full_duplex = false;

  // GBUSY is not only a status: writing it as 1 *starts* the frame (Linux
  // stmmac_mdio_format_addr() ORs MII_ADDR_GBUSY into the command).  Without
  // it no frame is sent at all, the bit reads back 0 at once, and the data
  // register just returns the zero written above -- a silent "dead PHY".
  std::uint32_t
  mdio_command (unsigned pa, unsigned gr, u32 goc)
  {
    return MDIO_ADDR_GBUSY | ((mdio_csr & 0xfu) << MDIO_ADDR_CR_SHIFT)
           | ((pa & 0x1fu) << MDIO_ADDR_PA_SHIFT)
           | ((gr & 0x1fu) << MDIO_ADDR_GR_SHIFT) | goc;
  }

  bool
  mdio_read (unsigned pa, unsigned gr, std::uint16_t& value)
  {
    (void)mdio_wait ();
    r (MAC_MDIO_DATA) = 0;
    r (MAC_MDIO_ADDR) = mdio_command (pa, gr, MDIO_ADDR_GOC_READ);

    // Wait for the frame to complete (GBUSY clears), then take the data.
    if (!mdio_wait ())
      {
        return false;
      }
    mdio_last_addr = r (MAC_MDIO_ADDR);
    mdio_last_data = r (MAC_MDIO_DATA);
    value = static_cast<std::uint16_t> (mdio_last_data & 0xffffu);
    // An undriven MDIO line (no PHY at this address, or the PHY not powered)
    // reads back as all ones through its pull-up.  Treat that as no answer:
    // 0xffff has bit 2 set, so it used to read as "link up".
    return value != 0xffffu;
  }

  bool
  mdio_write (unsigned pa, unsigned gr, std::uint16_t value)
  {
    if (!mdio_wait ())
      {
        return false;
      }
    r (MAC_MDIO_DATA) = value;
    r (MAC_MDIO_ADDR) = mdio_command (pa, gr, MDIO_ADDR_GOC_WRITE);
    return mdio_wait ();
  }

  // ------------------------------------------------------------- PHY link

  constexpr unsigned PHY_ADDR = 0; // jh7110-common.dtsi: ethernet-phy@0

  constexpr unsigned MII_BMCR = 0;
  constexpr unsigned MII_BMSR = 1;
  constexpr unsigned MII_PHYSID1 = 2;
  constexpr unsigned MII_PHYSID2 = 3;
  constexpr unsigned MII_ADVERTISE = 4;
  constexpr unsigned MII_LPA = 5;
  constexpr unsigned MII_CTRL1000 = 9;
  constexpr unsigned MII_STAT1000 = 10;

  constexpr std::uint16_t BMCR_RESET = 1u << 15;
  constexpr std::uint16_t BMCR_ANENABLE = 1u << 12;
  constexpr std::uint16_t BMCR_ANRESTART = 1u << 9;

  constexpr std::uint16_t BMSR_LSTATUS = 1u << 2;
  constexpr std::uint16_t BMSR_ANEGCOMPLETE = 1u << 5;

  bool
  bmsr_linked (void)
  {
    std::uint16_t v = 0;
    const bool answered = mdio_read (PHY_ADDR, MII_BMSR, v);
    ++cnt.phy_polls;
    return answered && (v & BMSR_LSTATUS) != 0;
  }

  // The PHY (a Motorcomm YT8531S, PHY ID 0x4f51:0xe91b): its delays and
  // drive strength are left as they are (the 1500 ps internal delays and
  // drive strength the Lite's device tree asks Linux to program gave more
  // packet loss here, measured 2/6 vs 4/6 echo replies), but the 1000 Mb/s
  // TX clock inversion is set here, not left to the boot chain (Linux:
  // motorcomm,tx-clk-1000-inverted; RGMII config 1, ext 0xA003, bit 14
  // TX_CLK_SEL). Measured 2026-11-27, with the PHY as a newer boot chain
  // left it (0xA003 = 0x00f1): bit 14 clear, no frame of the board's
  // reached the LAN at any TX delay (0..15); set, ping answered at every
  // delay.
  constexpr unsigned YT_EXT_ADDR = 0x1e;
  constexpr unsigned YT_EXT_DATA = 0x1f;
  constexpr std::uint16_t YT_RGMII_CONFIG1 = 0xa003;
  constexpr std::uint16_t YT_TX_CLK_SEL_INVERTED = 1u << 14;

  void
  phy_tx_clock (unsigned speed_mbps)
  {
    std::uint16_t v = 0;
    if (!mdio_write (PHY_ADDR, YT_EXT_ADDR, YT_RGMII_CONFIG1)
        || !mdio_read (PHY_ADDR, YT_EXT_DATA, v))
      {
        return;
      }
    const std::uint16_t nv
        = (speed_mbps == 1000) ? static_cast<std::uint16_t> (v | YT_TX_CLK_SEL_INVERTED)
                               : static_cast<std::uint16_t> (v & ~YT_TX_CLK_SEL_INVERTED);
    if (nv != v)
      {
        (void)mdio_write (PHY_ADDR, YT_EXT_ADDR, YT_RGMII_CONFIG1);
        (void)mdio_write (PHY_ADDR, YT_EXT_DATA, nv);
      }
  }
  //
  // Read the negotiated speed from the PHY's 1000BASE-T status and the
  // clause-37 link partner register; fall back to BMSR only.
  void
  read_speed (void)
  {
    std::uint16_t stat1000 = 0;
    std::uint16_t lpa = 0;
    st.full_duplex = true;
    st.speed_mbps = 100;

    if (mdio_read (PHY_ADDR, MII_STAT1000, stat1000)
        && (stat1000 & (1u << 11)) != 0) // link partner 1000BASE-T full duplex
      {
        st.speed_mbps = 1000;
        st.full_duplex = true;
        return;
      }
    if (mdio_read (PHY_ADDR, MII_LPA, lpa))
      {
        if ((lpa & (1u << 8)) != 0) // 100BASE-TX full duplex
          {
            st.speed_mbps = 100;
            st.full_duplex = true;
          }
        else if ((lpa & (1u << 7)) != 0) // 100BASE-TX half duplex
          {
            st.speed_mbps = 100;
            st.full_duplex = false;
          }
        else if ((lpa & (1u << 6)) != 0) // 10BASE-T full duplex
          {
            st.speed_mbps = 10;
            st.full_duplex = true;
          }
        else
          {
            st.speed_mbps = 10;
            st.full_duplex = false;
          }
      }
  }

  // Scan the MDIO bus: which of the 32 clause-22 addresses answer.  With
  // MDC dead every read returns 0x0000 or 0xffff, which is a different
  // failure from "the PHY sits at another address".
  void
  mdio_scan (char* out, std::size_t length)
  {
    std::size_t used = 0;
    used += static_cast<std::size_t> (
        std::snprintf (out + used, length - used, "mdio:"));
    for (unsigned pa = 0; pa < 4u && used + 10u < length; ++pa)
      {
        std::uint16_t id1 = 0xbeefu;
        std::uint16_t id2 = 0xbeefu;
        const bool ok = mdio_read (pa, MII_PHYSID1, id1)
                        && mdio_read (pa, MII_PHYSID2, id2);
        used += static_cast<std::size_t> (std::snprintf (
            out + used, length - used, " %u=%s%04x:%04x", pa,
            ok ? "" : "T", static_cast<unsigned> (id1),
            static_cast<unsigned> (id2)));
      }
    // Address 3 is the last one the clause-22 MDIO of a DW MAC needs to be
    // probed for; the PHY is at 0 on this board.  "T" means the busy bit
    // never cleared -- MDC is not running.
  }

  // ------------------------------------------------------------- DMA rings

  void
  ring_reset (void)
  {
    for (unsigned i = 0; i < RX_COUNT; ++i)
      {
        rx_ring[i] = {};
        rx_ring[i].w0 = static_cast<u32> (
            reinterpret_cast<std::uintptr_t> (rx_buf[i]));
        rx_ring[i].w3 = DESC_OWN | DESC_BUF1V;
      }
    for (unsigned i = 0; i < TX_COUNT; ++i)
      {
        tx_ring[i] = {};
      }
    rx_next = 0;
    tx_next = 0;
    vf2_dcache_flush (rx_ring, sizeof (rx_ring));
    vf2_dcache_flush (tx_ring, sizeof (tx_ring));
  }

  bool dma_reset (void); // defined below; the TX-mux probe uses it

  // ------------------------------------------------------- CRG bring-up
  //
  // GMAC0 is off after the boot chain (U-Boot only clocks it when it uses
  // the network), which is why GMAC VERSION reads 0x0000.  The JH7110 clock
  // controller gives every gate its own 32-bit register (Linux
  // clk-starfive-jh71x0.c: private->base + 4 * id, bit 31 = enable), and the
  // AON resets are one bit per reset in AONCRG+0x38.
  constexpr std::uintptr_t AONCRG = 0x17000000u;
  constexpr std::uintptr_t SYSCRG = 0x13020000u;
  constexpr std::uintptr_t AON_SYSCON = 0x17010000u;

  constexpr std::uint32_t CLK_ENABLE = 1u << 31;

  // starfive,jh7110-crg.h clock ids (AONCRG part).
  constexpr unsigned AONCLK_GMAC0_AHB = 2;
  constexpr unsigned AONCLK_GMAC0_AXI = 3;
  // ... and the two parents those gates hang from: the AXI/AHB "sticky
  // gate" and the APB bus.  Without the first of them the DMA's AXI port
  // has no clock, which is exactly a DMA soft reset that never clears while
  // register reads still work.
  constexpr unsigned AONCLK_END = 14;
  constexpr unsigned AONCLK_STG_AXIAHB = AONCLK_END + 3; // 17
  constexpr unsigned AONCLK_APB_BUS = AONCLK_END + 4;    // 18
  // ... and the SYSCRG part.
  constexpr unsigned SYSCLK_GMAC0_GTXCLK = 108;
  constexpr unsigned SYSCLK_GMAC0_PTP = 109;
  constexpr unsigned SYSCLK_GMAC0_GTXC = 111;

  // starfive,jh7110-crg.h reset ids (AONCRG +0x38).
  constexpr unsigned AONRST_GMAC0_AXI = 0;
  constexpr unsigned AONRST_GMAC0_AHB = 1;

  inline volatile u32&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile u32*> (a);
  }

  std::uint32_t clock_gate_read (std::uintptr_t crg, unsigned id)
  {
    return reg (crg + 4u * id);
  }

  bool
  clock_enable (std::uintptr_t crg, unsigned id)
  {
    const std::uintptr_t a = crg + 4u * id;
    reg (a) |= CLK_ENABLE;
    return (reg (a) & CLK_ENABLE) != 0;
  }

  // Board bring-up: power the whole GMAC0 clock tree the way Linux's
  // stmmac + clk-starfive drivers do, then pick the TX clock source that
  // actually feeds the MAC.
  // The system-bus chain the MAC's application clock hangs from:
  //   PLL2 -> BUS_ROOT (mux, id 5) -> AXI_CFG0 (div, id 7)
  //        -> STG_AXIAHB (div, id 8) -> the AON GMAC0_AXI gate
  // The CPU runs from NOCSTG_BUS (id 6), a *different* divider, so a zero
  // divider in the AXI branch leaves the CPU alive and the MAC without a
  // clock: registers readable through pclk, DMA and MDIO dead.
  constexpr unsigned SYSCLK_BUS_ROOT = 5;
  constexpr unsigned SYSCLK_AXI_CFG0 = 7;
  constexpr std::uint32_t CLK_MUX_SHIFT = 24;

  void
  axibus_report (std::uint32_t* before, std::uint32_t* after)
  {
    const std::uintptr_t br = SYSCRG + 4u * SYSCLK_BUS_ROOT;
    const std::uintptr_t ax = SYSCRG + 4u * SYSCLK_AXI_CFG0;
    // READ ONLY.  BUS_ROOT feeds the CPU's own bus (through NOCSTG_BUS):
    // switching its mux at run time wedges the hart, which is exactly what
    // the first attempt did.  Report the chain and let the log decide.
    before[0] = reg (br);
    before[1] = reg (ax);
    after[0] = reg (br);
    after[1] = reg (ax);
    (void)CLK_MUX_SHIFT;
  }

  bool
  board_bringup (void)
  {
    std::uint32_t bus_before[2];
    std::uint32_t bus_after[2];
    axibus_report (bus_before, bus_after);
    for (unsigned i = 0; i < 2; ++i)
      {
        bus_dump[i][0] = bus_before[i];
        bus_dump[i][1] = bus_after[i];
      }

    // AON gates (id 2,3,6,8) and system clocks (108,109,111).
    constexpr unsigned AONCLK_APB_FUNC = 1;
    (void)AONCLK_APB_FUNC;
    clock_enable (AONCRG, AONCLK_GMAC0_AXI);            // stmmaceth
    clock_enable (AONCRG, AONCLK_GMAC0_AHB);            // pclk
    // gmac0_tx_inv / gmac0_rx_inv are inverters, not gates (clk-starfive-
    // jh71x0.c: JH71X0__INV, bit 30 = invert; no enable bit). The TX one
    // is set to NOT inverted: what the boot chain leaves there varies
    // (measured 2026-11-27: 0x40000000 after U-Boot, inverted -- at 1 Gb/s
    // half a 125 MHz period off: frames received, none transmitted reaching
    // the LAN, no DHCP lease). The RX one is left as it is (receiving works).
    reg (AONCRG + 4u * 6u) = 0;
    clock_enable (SYSCRG, SYSCLK_GMAC0_GTXCLK);
    clock_enable (SYSCRG, SYSCLK_GMAC0_PTP);
    clock_enable (SYSCRG, SYSCLK_GMAC0_GTXC);

    // RX mux: parent 0 = the RGMII clock the PHY drives back (rgmii-id).
    reg (AONCRG + 4u * 7u) = CLK_ENABLE | (0u << 24);

    // Resets: one bit per reset, asserted when set.
    constexpr std::uintptr_t AON_RESET_ASSERT = AONCRG + 0x38;
    reg (AON_RESET_ASSERT) &= ~((1u << AONRST_GMAC0_AXI)
                                | (1u << AONRST_GMAC0_AHB));

    // GMAC0_TX parent (AONCRG clock 5, mux in bits 25:24): 0 = gtxclk (the
    // SoC-generated 125 MHz), 1 = gmac0_rmii_rtx.  The Lite's device tree
    // assigns the RMII_RTX parent together with starfive,tx-use-rgmii-clk --
    // the MAC does not drive the RGMII TX clock here, the PHY's clock does.
    // With parent 0 the link carries only some frames (measured: ~30-50%),
    // which is what an unsynchronised transmit clock looks like.
    reg (AONCRG + 4u * 5u) = CLK_ENABLE | (1u << 24);
    tx_mux = 1u;
    tx_mux_mdio[1] = true;
    tx_mux_ok[1] = true;

    return (reg (AON_RESET_ASSERT) & 0x3u) == 0;
  }

  bool
  dma_reset (void)
  {
    for (unsigned attempt = 0; attempt < 3u; ++attempt)
      {
        r (DMA_BUS_MODE) = DMA_BUS_SFT_RESET;
        const std::uint64_t end = now () + ticks (200);
        while (now () < end)
          {
            if ((r (DMA_BUS_MODE) & DMA_BUS_SFT_RESET) == 0)
              {
                return true;
              }
          }
      }
    return false;
  }

  u32
  tail (const void* ring, unsigned count)
  {
    return static_cast<u32> (reinterpret_cast<std::uintptr_t> (ring)
                             + count * sizeof (desc));
  }
} // namespace

namespace gmac
{
  void
  set_trace (trace_fn fn)
  {
    trace_hook = fn;
  }

  const info&
  status (void)
  {
    return st;
  }

  const char*
  last_error (void)
  {
    return err;
  }

  bool
  dma_reset_ok (void)
  {
    return diag_dma_ok;
  }

  // One line of raw state, for the console: the CRG gates, the resets, the
  // aon_syscon PHY-interface field, and the DMA/MTL registers.  A bring-up
  // log is worth more than a verdict when a block stays asleep.
  void
  dump_registers (char* out, std::size_t length)
  {
    std::snprintf (
        out, length,
        "aoncrg[2]=%08x [3]=%08x [17]=%08x [18]=%08x rst=%08x/%08x "
        "syscrg[8]=%08x [11]=%08x [108]=%08x [109]=%08x [111]=%08x "
        "busroot=%08x->%08x axicfg0=%08x->%08x syscon=%08x hwfeat0=%08x "
        "aon[5..8]=%08x,%08x,%08x,%08x txmux=%u dma=%u,%u mdio=%u,%u "
        "dmabus=%08x dmasts=%08x mtl_tx=%08x mtl_rx=%08x",
        reg (0x17000000u + 4u * 2u), reg (0x17000000u + 4u * 3u),
        reg (0x17000000u + 4u * 17u), reg (0x17000000u + 4u * 18u),
        reg (0x17000038u), reg (0x1700003Cu),
        reg (0x13020000u + 4u * 8u), reg (0x13020000u + 4u * 11u),
        reg (0x13020000u + 4u * 108u), reg (0x13020000u + 4u * 109u),
        reg (0x13020000u + 4u * 111u), bus_dump[0][0], bus_dump[0][1],
        bus_dump[1][0], bus_dump[1][1], reg (0x17010000u + 0xCu),
        r (0x11cu),
        reg (0x17000000u + 4u * 5u), reg (0x17000000u + 4u * 6u),
        reg (0x17000000u + 4u * 7u), reg (0x17000000u + 4u * 8u),
        tx_mux, tx_mux_ok[0] ? 1u : 0u, tx_mux_ok[1] ? 1u : 0u,
        tx_mux_mdio[0] ? 1u : 0u, tx_mux_mdio[1] ? 1u : 0u,
        r (DMA_BUS_MODE), r (DMA_STATUS), r (MTL_TXQ0_OP_MODE),
        r (MTL_RXQ0_OP_MODE));
    // PHY probe: read the control and status registers only.  (A clause-22
    // soft reset here would restart auto-negotiation *after* start() has
    // waited for it, dropping the link for the next three seconds.)
    std::uint16_t bmcr = 0xffffu;
    std::uint16_t bmsr = 0xffffu;
    (void)mdio_read (PHY_ADDR, MII_BMCR, bmcr);
    (void)mdio_read (PHY_ADDR, MII_BMSR, bmsr);
    const std::size_t n1 = std::strlen (out);
    std::snprintf (out + n1, length - n1,
                   " mdio: csr=%u last=%08x/%08x bmcr=%04x bmsr=%04x",
                   static_cast<unsigned> (mdio_csr), mdio_last_addr,
                   mdio_last_data, static_cast<unsigned> (bmcr),
                   static_cast<unsigned> (bmsr));
    const std::size_t n = std::strlen (out);
    if (n + 2u < length)
      {
        out[n] = ' ';
        mdio_scan (out + n + 1u, length - n - 1u);
      }
  }

  const counters&
  stats (void)
  {
    // MTL queue debug counters: an underflow means the TX FIFO ran dry while
    // the MAC was sending (a DMA burst/size problem), an RX overflow means the
    // FIFO could not be drained in time.  Both drop frames in hardware, which
    // no descriptor counter can see.
    cnt.tx_underflow = r (0x0d04u);      // MTL_TXQ0_UNDERFLOW
    cnt.rx_overflow = r (0x0d34u);       // MTL_RXQ0_MISSED_PACKET_OVERFLOW_CNT
    return cnt;
  }

  bool
  start (const std::uint8_t* mac)
  {
    set_error ("ok");

#ifdef NET_TEST_INHERIT_GMAC
    // The boot chain (U-Boot, after a `dhcp`) has already clocked GMAC0,
    // released its resets and brought the PHY up.  Take that state over: no
    // clock writes and no DMA soft reset (this core's SWR never clears when
    // we do it ourselves).  Only the queues, the rings and the MAC are ours.
    st.version = r (MAC_VERSION) & 0xffffu;
#else
    // The block is powered down after the boot chain: clock it and release
    // the resets before the first register read.
    (void)board_bringup ();
    st.version = r (MAC_VERSION) & 0xffffu;
#endif

    // 1. One microsecond tick for the MAC's watchdog timers.  The CSR clock
    // on GMAC0 is 100 MHz in the JH7110 (aoncrg GMAC0_AHB); Linux computes
    // this from the clock rate: (100 MHz / 1 MHz) - 1 = 99 = 0x63.
    r (MAC_ONEUS_TIC) = 0x63;

    // 2. Stop and reset the DMA.
    r (DMA_CH0_TX_CTRL) = 0;
    r (DMA_CH0_RX_CTRL) = 0;
    bool dma_ok = true;
#ifndef NET_TEST_INHERIT_GMAC
    dma_ok = dma_reset ();
#endif
    diag_dma_ok = dma_ok;
    if (!dma_ok)
      {
        // Not fatal on its own: report it and try to run anyway, so the log
        // still says whether MDIO and the PHY came up.
        set_error ("DMA soft reset did not clear");
      }

    // Fixed burst, as the device tree asks (snps,fixed-burst, no pbl-x8).
    r (DMA_SYS_BUS_MODE) = DMA_SYS_FB;

    // 3. Queues: store-and-forward, receive queue size 2048/256-1 = 7.
    r (MTL_TXQ0_OP_MODE) = MTL_TX_TSF | MTL_OP_TXQEN;
    r (MTL_RXQ0_OP_MODE) = MTL_RX_RSF | (7u << 20) | MTL_OP_RXQEN;

    // 4. Channel: 16-beat bursts on both directions, no 8x PBL.
    r (DMA_CH0_CTRL) &= ~DMA_CH_CTRL_PBLX8;
    r (DMA_CH0_TX_CTRL) = 16u << DMA_CH_PBL_SHIFT;
    // RX: burst length and the receive buffer size, which is a field of its
    // own (bits 14..1, in bytes).  Linux's default is 1536.
    r (DMA_CH0_RX_CTRL) = (16u << DMA_CH_PBL_SHIFT) | (1536u << 1);

    // 5. Rings.
    ring_reset ();
    r (DMA_CH0_TX_BASE_HI) = 0;
    r (DMA_CH0_TX_BASE) = static_cast<u32> (
        reinterpret_cast<std::uintptr_t> (tx_ring));
    r (DMA_CH0_RX_BASE_HI) = 0;
    r (DMA_CH0_RX_BASE) = static_cast<u32> (
        reinterpret_cast<std::uintptr_t> (rx_ring));
    // The whole receive ring belongs to the DMA at start: the tail is one
    // entry past the last descriptor.  Writing `base + 0` here would tell the
    // DMA it has no free descriptors, and it would never fill one.
    r (DMA_CH0_TX_END) = tail (tx_ring, 0);        // nothing queued yet
    r (DMA_CH0_RX_END) = tail (rx_ring, RX_COUNT);
    r (DMA_CH0_TX_RING_LEN) = TX_COUNT - 1u;
    r (DMA_CH0_RX_RING_LEN) = RX_COUNT - 1u;
    r (DMA_CH0_INTR_ENA) = 0;    // polled
    r (DMA_CH0_RX_WD) = 0;

    // 6. PHY: identify, restart auto-negotiation, wait for link.

    // MDC divider (MDIO_ADDR 11:8): 8 is the slowest the CSR range offers, so
    // MDC stays far below the 2.5 MHz the standard asks for whatever the
    // AXI-side clock turns out to be.  (Every divider answers on this board.)
    mdio_csr = 8u;

    std::uint16_t id1 = 0;
    std::uint16_t id2 = 0;
    if (!mdio_read (PHY_ADDR, MII_PHYSID1, id1)
        || !mdio_read (PHY_ADDR, MII_PHYSID2, id2) || id1 == 0x0000u)
      {
        set_error ("no answer on MDIO (PHY address 0)");
        return false;
      }
    st.phy_id1 = id1;
    st.phy_id2 = id2;

    // The PHY is otherwise left alone: its timing and the link the board
    // came up with are what works here.  (Every other write tried on it --
    // AN restart, RGMII delay, drive strength, forced speed -- made the data
    // path worse; see visionfive2-lite-net.md.)  Only the 1000 Mb/s TX
    // clock inversion is set, below and in apply_speed().

    (void)mdio_read (PHY_ADDR, MII_BMSR, st.bmsr);
    st.link = wait_ms (6000, [] {
      std::uint16_t v = 0;
      // Both bits matter: link alone can be the previous negotiation, still
      // up for a moment after the restart above.
      return mdio_read (PHY_ADDR, MII_BMSR, v)
             && (v & BMSR_LSTATUS) != 0 && (v & BMSR_ANEGCOMPLETE) != 0;
    });
    (void)mdio_read (PHY_ADDR, MII_BMSR, st.bmsr_after);
    if (st.link)
      {
        read_speed ();
        phy_tx_clock (st.speed_mbps);
      }

    // 7. MAC: address, filter, speed/duplex, then enable transmit/receive.
    const std::uint8_t default_mac[6] = {0x02, 0x56, 0x46, 0x32, 0x4c, 0x01};
    const std::uint8_t* m = (mac != nullptr) ? mac : default_mac;
    r (MAC_ADDR_HIGH) = (static_cast<u32> (m[5]) << 8)
                        | static_cast<u32> (m[4]);
    r (MAC_ADDR_LOW) = (static_cast<u32> (m[3]) << 24)
                       | (static_cast<u32> (m[2]) << 16)
                       | (static_cast<u32> (m[1]) << 8)
                       | static_cast<u32> (m[0]);
    r (MAC_PACKET_FILTER) = MAC_FILTER_RA | MAC_FILTER_PM;

    {
      char d[768];
      dump_registers (d, sizeof (d));
      trace ("[gmac] regs ");
      trace (d);
      trace ("\n");
    }
    u32 config = r (MAC_CONFIG) | MAC_CORE_INIT;
    config &= ~(MAC_CONFIG_FES | MAC_CONFIG_DM);
    if (st.speed_mbps == 1000)
      {
        config |= MAC_CONFIG_FES;
      }
    if (st.link && st.full_duplex)
      {
        config |= MAC_CONFIG_DM;
      }
    applied_speed = st.link ? st.speed_mbps : 0;
    applied_full_duplex = st.link && st.full_duplex;
    r (MAC_CONFIG) = config;

    // 8. Start the DMA, then the MAC.
    r (DMA_CH0_RX_CTRL) |= DMA_CH_RX_ST;
    r (DMA_CH0_TX_CTRL) |= DMA_CH_TX_ST;
    r (MAC_CONFIG) = config | MAC_CONFIG_RE | MAC_CONFIG_TE;

    return true;
  }

  // Speed and duplex are *not* constant: the PHY is still negotiating when
  // start() runs on a cold board, and the MAC then latched 100/half.  On a
  // 1 Gb/s link that leaves the transmitter sending at the wrong rate: the
  // frames never appear on the wire (no error anywhere -- the DMA drains its
  // ring happily), while reception keeps working because the RGMII receive
  // clock does not depend on FES.  So re-apply them whenever they change.
  void
  apply_speed (void)
  {
    if (!st.link)
      {
        return;
      }
    if (applied_speed == st.speed_mbps
        && applied_full_duplex == st.full_duplex)
      {
        return;
      }
    phy_tx_clock (st.speed_mbps);
    u32 c = r (MAC_CONFIG) & ~(MAC_CONFIG_FES | MAC_CONFIG_DM);
    if (st.speed_mbps == 1000)
      {
        c |= MAC_CONFIG_FES;
      }
    if (st.full_duplex)
      {
        c |= MAC_CONFIG_DM;
      }
    r (MAC_CONFIG) = c;
    applied_speed = st.speed_mbps;
    applied_full_duplex = st.full_duplex;
  }

  bool
  link_up (void)
  {
    std::uint16_t v = 0;
    if (!mdio_read (PHY_ADDR, MII_BMSR, v))
      {
        st.link = false;
        return false;
      }
    ++cnt.phy_polls;
    st.link = (v & BMSR_LSTATUS) != 0;
    if (st.link)
      {
        read_speed ();
        apply_speed ();
      }
    return st.link;
  }

  bool
  send (const std::uint8_t* frame, std::size_t length)
  {
    if (length < 14 || length > TX_BUFFER)
      {
        return false;
      }
    // A free descriptor: the DMA clears OWN when it has read the buffer.
    for (unsigned spin = 0; spin < 2000u; ++spin)
      {
        const unsigned i = tx_next;
        vf2_dcache_flush (&tx_ring[i], sizeof (desc)); // fresh OWN
        if ((tx_ring[i].w3 & DESC_OWN) == 0)
          {
            std::memcpy (tx_buf[i], frame, length);
            // Zero up to the 4-byte boundary so no stale bytes reach the wire.
            const std::size_t padded = (length + 3u) & ~std::size_t{3};
            if (padded > length)
              {
                std::memset (tx_buf[i] + length, 0, padded - length);
              }
            vf2_dcache_flush (tx_buf[i], padded);
            tx_ring[i].w0 = static_cast<u32> (
                reinterpret_cast<std::uintptr_t> (tx_buf[i]));
            tx_ring[i].w1 = 0;
            tx_ring[i].w2 = static_cast<u32> (length) & DESC_SIZE_MASK;
            tx_ring[i].w3 = DESC_OWN | DESC_FD | DESC_LD
                            | (static_cast<u32> (length) & DESC_SIZE_MASK);
            vf2_dcache_flush (&tx_ring[i], sizeof (desc));
            tx_next = (i + 1u) % TX_COUNT;
            // Kick the DMA: the tail pointer is one past the last enqueued.
            r (DMA_CH0_TX_END) = tail (tx_ring, tx_next);
            ++cnt.tx_frames;
            return true;
          }
        ++cnt.tx_busy;
      }
    ++cnt.tx_errors;
    set_error ("no free TX descriptor");
    return false;
  }

  int
  poll (std::uint8_t* out, std::size_t max)
  {
    const unsigned i = rx_next;
    vf2_dcache_flush (&rx_ring[i], sizeof (desc));
    if ((rx_ring[i].w3 & DESC_OWN) != 0)
      {
        return -1; // the DMA still owns it
      }

    const u32 status = rx_ring[i].w3;
    int result = -1;
    if ((status & (DESC_ERR_SUMMARY | DESC_RX_CRC | DESC_RX_OVF)) != 0)
      {
        ++cnt.rx_errors;
      }
    else
      {
        const std::size_t length = status & DESC_SIZE_MASK;
        vf2_dcache_flush (rx_buf[i], static_cast<std::uint32_t> (length));
        if (length > max)
          {
            ++cnt.rx_dropped;
          }
        else
          {
            std::memcpy (out, rx_buf[i], length);
            result = static_cast<int> (length);
            ++cnt.rx_frames;
          }
      }

    // Hand the buffer back to the DMA.
    rx_ring[i].w0 = static_cast<u32> (
        reinterpret_cast<std::uintptr_t> (rx_buf[i]));
    rx_ring[i].w3 = DESC_OWN | DESC_BUF1V;
    vf2_dcache_flush (&rx_ring[i], sizeof (desc));
    rx_next = (i + 1u) % RX_COUNT;
    r (DMA_CH0_RX_END) = tail (rx_ring, rx_next);
    return result;
  }
} // namespace gmac
