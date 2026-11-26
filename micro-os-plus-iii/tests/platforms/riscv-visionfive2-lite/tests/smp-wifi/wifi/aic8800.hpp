/*
 * aic8800.hpp -- the AIC8800D80/D40 Wi-Fi chip of the VisionFive 2 Lite's
 * module (LB-LINK BL-M8800DS2), below the 802.11 logic: the SDIO function,
 * the frame format on the two FIFOs, the firmware download through the boot
 * ROM, and request/confirm messages to the firmware's kernel.
 *
 * The chip is a FullMAC: the firmware scans, authenticates, associates and
 * encrypts.  The host sends it requests ("LMAC messages": id, destination
 * task, parameters), receives confirmations and indications, and exchanges
 * Ethernet payloads with a small descriptor in front.  wlan.cpp is that
 * upper half; this file only moves bytes.
 *
 * Wire formats and constants come from the vendor's Linux driver
 * (aic8800_bsp/aicsdio.c, aic_bsp_driver.c, aic8800d80_compat.c and
 * aic8800_fdrv/aicwf_sdio.c, aicwf_txrxif.c); see README.md for the
 * revision.  The layouts used here were printed from the vendor headers with
 * a host compiler, not counted by hand.
 *
 * NOT YET RUN ON THE BOARD.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include "sdio1.hpp"

#include <cstddef>
#include <cstdint>

namespace aic8800
{
  struct chip
  {
    sdio1::info sdio{};
    std::uint16_t vendor{ 0 };
    std::uint16_t device{ 0 };
    std::uint32_t chip_id_word{ 0 }; // boot ROM, memory 0x40500000
    std::uint8_t revision{ 0 };      // 1 = U01, 3 = U02, 7 = U03
    bool id_h{ false };              // the "H" variant (other firmware)
    std::uint32_t fw_size{ 0 };
    std::uint32_t fw_rom_version{ 0 }; // image word at +0x1C
    std::uint32_t boot_status{ 0 };
  };

  struct counters
  {
    std::uint32_t rx_reads{ 0 };     // CMD53 reads
    std::uint32_t rx_data{ 0 };      // data frames
    std::uint32_t rx_messages{ 0 };  // confirmations + indications
    std::uint32_t rx_prints{ 0 };    // firmware log lines
    std::uint32_t rx_other{ 0 };     // function 2 / byte mode / unknown
    std::uint32_t tx_data{ 0 };
    std::uint32_t tx_messages{ 0 };
    std::uint32_t tx_busy{ 0 };      // flow control said no
    std::uint32_t io_errors{ 0 };
    std::uint32_t timeouts{ 0 };     // a request without its confirmation
  };

  /// SDIO, function 1, boot ROM, firmware download, start.  On success the
  /// firmware is running and answers MM_* messages.
  bool
  boot (chip& out);

  [[nodiscard]] bool
  running (void);

  /// Send a request and wait for its confirmation.  `cfm`/`cfm_max` receive
  /// the confirmation's parameters (may be nullptr/0).  Frames that arrive
  /// meanwhile are dispatched to the handlers below.
  bool
  request (std::uint16_t id, std::uint16_t dest_task, const void* param,
           std::uint16_t param_len, std::uint16_t cfm_id, void* cfm = nullptr,
           std::uint16_t cfm_max = 0, std::uint32_t timeout_ms = 2000);

  /// Send one Ethernet payload.  `ethertype` points at the two bytes as they
  /// are in the frame.  tid 0xFF = not a QoS frame.
  bool
  send_data (const std::uint8_t* dst, const std::uint8_t* src,
             const std::uint8_t* ethertype, const std::uint8_t* payload,
             std::uint32_t length, std::uint8_t vif, std::uint8_t sta,
             std::uint8_t tid);

  /// Read whatever the chip has for the host and dispatch it.  Returns the
  /// number of frames handled.
  unsigned
  poll (void);

  /// A message that is not the confirmation being waited for.
  using message_handler = void (*) (std::uint16_t id,
                                    const std::uint8_t* param,
                                    std::uint16_t param_len);
  void
  set_message_handler (message_handler handler);

  /// One received data frame: the 60-byte RX header, then the 802.11 MPDU.
  using data_handler = void (*) (const std::uint8_t* frame,
                                 std::uint32_t length);
  void
  set_data_handler (data_handler handler);

  [[nodiscard]] const counters&
  stats (void);

  [[nodiscard]] const char*
  last_error (void);

  // ---- constants shared with wlan.cpp ------------------------------------
  inline constexpr std::uint32_t rx_header = 60; // RX_HWHRD_LEN

  // LMAC tasks and the driver's own task id (lmac_msg.h).
  inline constexpr std::uint16_t TASK_MM = 0, TASK_DBG = 1, TASK_SCANU = 4,
                                 TASK_ME = 5, TASK_SM = 6;
  inline constexpr std::uint16_t DRV_TASK_ID = 100;
} // namespace aic8800
