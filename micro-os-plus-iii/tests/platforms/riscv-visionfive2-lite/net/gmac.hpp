/*
 * gmac.h -- VisionFive 2 Lite gigabit Ethernet: Synopsys DesignWare GMAC 5.20
 * (jh7110.dtsi: gmac0@16030000, "snps,dwmac-5.20", RGMII to a PHY at MDIO
 * address 0, "rgmii-id" -- the delays live in the PHY).
 *
 * Polled, no interrupts: the receive thread calls poll(), the transmit path
 * is a blocking submit.  The register names and bit positions are those of
 * Linux's drivers/net/ethernet/stmicro/stmmac/{dwmac4,dwmac4_dma,dwmac4_descs}.h.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace gmac
{
  inline constexpr std::uintptr_t base = 0x16030000u;

  struct info
  {
    std::uint32_t version{0};    // GMAC VERSION (0x110): 5.20 -> 0x52
    std::uint32_t phy_id1{0};
    std::uint32_t phy_id2{0};
    std::uint16_t bmsr{0};
    std::uint16_t bmsr_after{0};
    bool link{false};
    bool full_duplex{true};
    unsigned speed_mbps{0};
  };

  /// Where the bring-up log goes (the test installs its console writer).
  using trace_fn = void (*) (const char* text);
  void
  set_trace (trace_fn fn);

  /// Reset and start the MAC and its DMA rings.  The module's clocks,
  /// resets, pinmux and RGMII delays are assumed to be as the boot chain
  /// left them (U-Boot enables GMAC0 for its own network use); the PHY is
  /// re-negotiated here.  `mac` may be nullptr for the built-in address.
  bool
  start (const std::uint8_t* mac);

  [[nodiscard]] const info&
  status (void);

  [[nodiscard]] const char*
  last_error (void);

  /// False when the DMA soft reset never cleared (the MAC can still read).
  [[nodiscard]] bool
  dma_reset_ok (void);

  /// A raw register line for the bring-up log (CRG gates, resets, DMA/MTL).
  void
  dump_registers (char* out, std::size_t length);

  /// Hand one Ethernet frame to the DMA.  Blocks (bounded) until the
  /// descriptor is free again, which is what a polled driver can do.
  bool
  send (const std::uint8_t* frame, std::size_t length);

  /// Take one received frame, or -1 when the ring is empty.  Never returns a
  /// frame longer than `max`.
  int
  poll (std::uint8_t* out, std::size_t max);

  struct counters
  {
    std::uint32_t rx_frames{0};
    std::uint32_t tx_frames{0};
    std::uint32_t rx_errors{0};
    std::uint32_t tx_errors{0};
    std::uint32_t rx_dropped{0};
    std::uint32_t tx_busy{0};
    std::uint32_t phy_polls{0};
    std::uint32_t tx_underflow{0}; // MTL TX FIFO underflow events
    std::uint32_t rx_overflow{0};  // MTL RX FIFO dropped packets
  };

  [[nodiscard]] const counters&
  stats (void);

  /// True when the PHY reports link, refreshing the cached status.
  bool
  link_up (void);

} // namespace gmac
