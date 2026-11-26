/*
 * wlan.cpp -- the 802.11 station on the AIC8800 FullMAC (wlan.hpp).
 *
 * Three parts:
 *
 *   1. the firmware's configuration after it boots -- the message sequence
 *      of the vendor driver's rwnx_cfg80211_init() and rwnx_open();
 *   2. scan and connect -- SCANU_START_REQ / SM_CONNECT_REQ and the
 *      indications that answer them;
 *   3. WPA2-PSK -- the 4-way handshake, taken from the MangoPi MQ-Pro's wlan
 *      layer (same EAPOL code, same crypto.hpp), ending in MM_KEY_ADD_REQ
 *      instead of software CCMP: this chip encrypts by itself.
 *
 * Messages are built as byte arrays at the offsets the vendor's structures
 * have (lmac_msg.h, printed with offsetof on a host compiler; README.md).
 * The offset of every field written is named next to it.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "wlan.hpp"

#include "aic8800.hpp"
#include "wifi-platform.hpp"

#include <clint.hpp>

// SHA-1, HMAC, PBKDF2, the 802.11 PRF, AES key unwrap: the MangoPi MQ-Pro's
// header-only, compile-time-tested implementation (tests.cmake adds its
// directory to this test's include path).
#include <crypto.hpp>

#include <cstring>

namespace crypto = mqpro::crypto;

namespace
{
  using u8 = std::uint8_t;
  using u16 = std::uint16_t;
  using u32 = std::uint32_t;
  using u64 = std::uint64_t;

  // ---- lmac_msg.h: message ids --------------------------------------------
  constexpr u16 MM_RESET_REQ = 0, MM_RESET_CFM = 1;
  constexpr u16 MM_START_REQ = 2, MM_START_CFM = 3;
  constexpr u16 MM_VERSION_REQ = 4, MM_VERSION_CFM = 5;
  constexpr u16 MM_ADD_IF_REQ = 6, MM_ADD_IF_CFM = 7;
  constexpr u16 MM_KEY_ADD_REQ = 36, MM_KEY_ADD_CFM = 37;
  constexpr u16 MM_SET_RF_CALIB_REQ = 105, MM_SET_RF_CALIB_CFM = 106;
  constexpr u16 MM_GET_MAC_ADDR_REQ = 115, MM_GET_MAC_ADDR_CFM = 116;
  constexpr u16 MM_SET_TXPWR_IDX_LVL_REQ = 119, MM_SET_TXPWR_IDX_LVL_CFM = 120;
  constexpr u16 MM_SET_STACK_START_REQ = 123, MM_SET_STACK_START_CFM = 124;
  constexpr u16 MM_GET_FW_VERSION_REQ = 128, MM_GET_FW_VERSION_CFM = 129;
  constexpr u16 SCANU_START_REQ = 0x1000, SCANU_START_CFM = 0x1001;
  constexpr u16 SCANU_RESULT_IND = 0x1004;
  constexpr u16 SCANU_START_CFM_ADDTIONAL = 0x1009;
  constexpr u16 ME_CONFIG_REQ = 0x1400, ME_CONFIG_CFM = 0x1401;
  constexpr u16 ME_CHAN_CONFIG_REQ = 0x1402, ME_CHAN_CONFIG_CFM = 0x1403;
  constexpr u16 ME_SET_CONTROL_PORT_REQ = 0x1404;
  constexpr u16 ME_SET_CONTROL_PORT_CFM = 0x1405;
  constexpr u16 SM_CONNECT_REQ = 0x1800, SM_CONNECT_CFM = 0x1801;
  constexpr u16 SM_CONNECT_IND = 0x1802;
  constexpr u16 SM_DISCONNECT_REQ = 0x1803, SM_DISCONNECT_CFM = 0x1804;
  constexpr u16 SM_DISCONNECT_IND = 0x1805;

  // ---- lmac_mac.h ----------------------------------------------------------
  constexpr u8 PHY_BAND_2G4 = 0, PHY_BAND_5G = 1;
  constexpr u8 CHAN_NO_IR = 1u << 0, CHAN_RADAR = 1u << 2;
  constexpr u8 MAC_CIPHER_TKIP = 1, MAC_CIPHER_CCMP = 2;
  constexpr u32 CONTROL_PORT_HOST = 1u << 0, WPA_WPA2_IN_USE = 1u << 3;
  constexpr u8 MM_STA = 0;
  constexpr u8 INVALID_STA = 0xFF;

  // The vendor driver's command timeout (RWNX_80211_CMD_TIMEOUT_MS).
  constexpr u32 CMD_TIMEOUT_MS = 6000;

  constexpr u8 ethertype_eapol[2] = { 0x88, 0x8e };
  constexpr u8 broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

  // What this station offers: RSN, CCMP pairwise, PSK; the group cipher is
  // the AP's (CCMP, or TKIP in a mixed WPA/WPA2 network).  The same element
  // goes into the association request and into message 2 of the handshake.
  constexpr u8 rsn_element_ccmp[] = {
    0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00,
    0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
  };
  constexpr u8 rsn_element_tkip[] = {
    0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x01, 0x00, 0x00,
    0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
  };

  // ---- the channel plan ----------------------------------------------------
  struct channel
  {
    u16 freq;
    u8 band;
    u8 flags;
  };
  // 5260..5720 MHz are DFS channels: listen only, never probe (the vendor
  // driver marks them the same way when built with radar detection).
  constexpr u8 DFS = CHAN_NO_IR | CHAN_RADAR;
  constexpr channel channels[] = {
    { 2412, PHY_BAND_2G4, 0 }, { 2417, PHY_BAND_2G4, 0 },
    { 2422, PHY_BAND_2G4, 0 }, { 2427, PHY_BAND_2G4, 0 },
    { 2432, PHY_BAND_2G4, 0 }, { 2437, PHY_BAND_2G4, 0 },
    { 2442, PHY_BAND_2G4, 0 }, { 2447, PHY_BAND_2G4, 0 },
    { 2452, PHY_BAND_2G4, 0 }, { 2457, PHY_BAND_2G4, 0 },
    { 2462, PHY_BAND_2G4, 0 }, { 2467, PHY_BAND_2G4, 0 },
    { 2472, PHY_BAND_2G4, 0 },
    { 5180, PHY_BAND_5G, 0 },   { 5200, PHY_BAND_5G, 0 },
    { 5220, PHY_BAND_5G, 0 },   { 5240, PHY_BAND_5G, 0 },
    { 5260, PHY_BAND_5G, DFS }, { 5280, PHY_BAND_5G, DFS },
    { 5300, PHY_BAND_5G, DFS }, { 5320, PHY_BAND_5G, DFS },
    { 5500, PHY_BAND_5G, DFS }, { 5520, PHY_BAND_5G, DFS },
    { 5540, PHY_BAND_5G, DFS }, { 5560, PHY_BAND_5G, DFS },
    { 5580, PHY_BAND_5G, DFS }, { 5600, PHY_BAND_5G, DFS },
    { 5620, PHY_BAND_5G, DFS }, { 5640, PHY_BAND_5G, DFS },
    { 5660, PHY_BAND_5G, DFS }, { 5680, PHY_BAND_5G, DFS },
    { 5700, PHY_BAND_5G, DFS }, { 5720, PHY_BAND_5G, DFS },
    { 5745, PHY_BAND_5G, 0 },   { 5765, PHY_BAND_5G, 0 },
    { 5785, PHY_BAND_5G, 0 },   { 5805, PHY_BAND_5G, 0 },
    { 5825, PHY_BAND_5G, 0 },
  };
  constexpr unsigned channel_count = sizeof (channels) / sizeof (channels[0]);
  constexpr std::int8_t channel_tx_power = 20; // dBm
  // SCAN_CHANNEL_MAX 42, MAC_DOMAINCHANNEL_24G_MAX 14, _5G_MAX 28.
  static_assert (channel_count <= 42);

  // ---- state ---------------------------------------------------------------
  enum class phase
  {
    idle,
    connecting,
    handshake,
    linked
  };

  bool chip_started = false;
  phase state = phase::idle;
  aic8800::chip chip{};
  wlan::mac_address own_address{};
  u8 vif_index = 0;
  u8 ap_index = INVALID_STA;
  bool ap_qos = false;
  wlan::counters stat{};
  wlan::receive_handler on_receive = nullptr;
  wlan::link_handler on_link = nullptr;

  std::array<wlan::bss, 48> bss_table{};
  std::size_t bss_count = 0;
  wlan::bss target{};

  // Events the firmware raised inside aic8800::poll(); acted on afterwards,
  // at the top level, where a request to the firmware is allowed again.
  volatile bool scan_done = false;
  bool connect_event = false;
  u16 connect_status = 0;
  bool disconnect_event = false;
  u16 disconnect_reason = 0;

  struct eapol_slot
  {
    u16 length{ 0 };
    u8 data[640];
  };
  eapol_slot eapol_queue[4];
  unsigned eapol_head = 0, eapol_tail = 0;

  // ---- WPA2 ----------------------------------------------------------------
  std::array<u8, 32> pmk{};
  std::array<u8, 48> ptk{}; // KCK | KEK | TK
  std::array<u8, 32> anonce{};
  std::array<u8, 32> snonce{};
  u32 nonce_counter = 0;
  bool ptk_derived = false;
  bool handshake_done = false;
  bool handshake_failed = false;
  bool port_open = false;
  u64 replay_counter = 0;
  bool replay_valid = false;

  alignas (8) u8 eth_buf[1700];

  // ---- small helpers ---------------------------------------------------------
  u16
  get16 (const u8* p)
  {
    return static_cast<u16> (p[0] | (p[1] << 8));
  }

  u32
  get32 (const u8* p)
  {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<u32> (p[3]) << 24);
  }

  u16
  get16be (const u8* p)
  {
    return static_cast<u16> ((p[0] << 8) | p[1]);
  }

  void
  put16 (u8* p, u16 v)
  {
    p[0] = static_cast<u8> (v);
    p[1] = static_cast<u8> (v >> 8);
  }

  void
  put32 (u8* p, u32 v)
  {
    put16 (p, static_cast<u16> (v));
    put16 (p + 2, static_cast<u16> (v >> 16));
  }

  void
  put16be (u8* p, u16 v)
  {
    p[0] = static_cast<u8> (v >> 8);
    p[1] = static_cast<u8> (v);
  }

  unsigned
  channel_of (u16 freq)
  {
    if (freq == 2484)
      {
        return 14;
      }
    if (freq >= 2412 && freq <= 2472)
      {
        return (freq - 2407u) / 5u;
      }
    if (freq >= 5000)
      {
        return (freq - 5000u) / 5u;
      }
    return 0;
  }

  bool
  ask (u16 id, u16 task, const void* param, u16 len, u16 cfm_id,
       void* cfm = nullptr, u16 cfm_max = 0)
  {
    if (aic8800::request (id, task, param, len, cfm_id, cfm, cfm_max,
                          CMD_TIMEOUT_MS))
      {
        return true;
      }
    kprintf ("[wlan] %s\n", aic8800::last_error ());
    return false;
  }

  // ---- part 1: configuration ---------------------------------------------

  // rwnx_cfg80211_init() then rwnx_open(), for PRODUCT_ID_AIC8800D80.
  bool
  configure (void)
  {
    // rwnx_send_set_stack_start_req (1, 0, CO_BIT (5), 0).
    {
      const u8 req[4] = { 1, 0, 1u << 5, 0 };
      u8 cfm[2] = {};
      if (!ask (MM_SET_STACK_START_REQ, aic8800::TASK_MM, req, sizeof (req),
                MM_SET_STACK_START_CFM, cfm, sizeof (cfm)))
        {
          return false;
        }
    }
    // rwnx_send_get_fw_version_req(): { len, text[63] }.
    {
      u8 cfm[64] = {};
      if (!ask (MM_GET_FW_VERSION_REQ, aic8800::TASK_MM, nullptr, 0,
                MM_GET_FW_VERSION_CFM, cfm, sizeof (cfm)))
        {
          return false;
        }
      const unsigned n = cfm[0] < 63u ? cfm[0] : 63u;
      kprintf ("[wlan] firmware \"%.*s\"\n", static_cast<int> (n),
               reinterpret_cast<const char*> (cfm + 1));
    }
    // rwnx_ic_rf_init() -> aicwf_set_rf_config_8800d80():
    // rwnx_send_txpwr_lvl_v3_req() with the driver's built-in table
    // (rwnx_platform.c userconfig_info.txpwr_lvl_v3), then the calibration.
    {
      u8 req[95] = {}; // sizeof (struct mm_set_txpwr_lvl_req)
      constexpr std::int8_t b_ag_2g4[12]
          = { 20, 20, 20, 20, 20, 20, 20, 20, 18, 18, 16, 16 };
      constexpr std::int8_t n_ac_2g4[10]
          = { 20, 20, 20, 20, 18, 18, 16, 16, 16, 16 };
      constexpr std::int8_t ax_2g4[12]
          = { 20, 20, 20, 20, 18, 18, 16, 16, 16, 16, 15, 15 };
      constexpr std::int8_t a_5g[12]
          = { -128, -128, -128, -128, 20, 20, 20, 20, 18, 18, 16, 16 };
      constexpr std::int8_t n_ac_5g[10]
          = { 20, 20, 20, 20, 18, 18, 16, 16, 16, 15 };
      constexpr std::int8_t ax_5g[12]
          = { 20, 20, 20, 20, 18, 18, 16, 16, 16, 15, 14, 14 };
      req[0] = 1;                                // enable            @0
      std::memcpy (req + 1, b_ag_2g4, 12);       // pwrlvl_11b_11ag_2g4 @1
      std::memcpy (req + 13, n_ac_2g4, 10);      // pwrlvl_11n_11ac_2g4 @13
      std::memcpy (req + 23, ax_2g4, 12);        // pwrlvl_11ax_2g4     @23
      std::memcpy (req + 35, a_5g, 12);          // pwrlvl_11a_5g       @35
      std::memcpy (req + 47, n_ac_5g, 10);       // pwrlvl_11n_11ac_5g  @47
      std::memcpy (req + 57, ax_5g, 12);         // pwrlvl_11ax_5g      @57
      if (!ask (MM_SET_TXPWR_IDX_LVL_REQ, aic8800::TASK_MM, req, sizeof (req),
                MM_SET_TXPWR_IDX_LVL_CFM))
        {
          return false;
        }
    }
    {
      u8 req[24] = {};               // struct mm_set_rf_calib_req
      put32 (req + 0, 0x0f8fu);      // cal_cfg_24g
      put32 (req + 4, 0x0f0fu);      // cal_cfg_5g
      put32 (req + 8, 0x0c34c008u);  // param_alpha
      put32 (req + 12, 0);           // bt_calib_en
      put32 (req + 16, 0x264203u);   // bt_calib_param
      // xtal_cap @20, xtal_cap_fine @21 = 0: the module's own trim
      u8 cfm[16] = {};
      if (!ask (MM_SET_RF_CALIB_REQ, aic8800::TASK_MM, req, sizeof (req),
                MM_SET_RF_CALIB_CFM, cfm, sizeof (cfm)))
        {
          return false;
        }
    }
    // rwnx_send_get_macaddr_req(): the address in the module's eFuse.
    {
      u8 req[4] = {};
      put32 (req, 1); // get
      u8 cfm[6] = {};
      if (!ask (MM_GET_MAC_ADDR_REQ, aic8800::TASK_MM, req, sizeof (req),
                MM_GET_MAC_ADDR_CFM, cfm, sizeof (cfm)))
        {
          return false;
        }
      if ((cfm[0] | cfm[1] | cfm[2] | cfm[3]) != 0)
        {
          std::memcpy (own_address.data (), cfm, 6);
        }
      else
        {
          // An unprogrammed eFuse: a locally administered address.
          own_address = { 0x02, 0x56, 0x46, 0x4c, 0x57, 0x01 };
          kprintf ("[wlan] no MAC address in the eFuse, using a local one\n");
        }
    }
    if (!ask (MM_RESET_REQ, aic8800::TASK_MM, nullptr, 0, MM_RESET_CFM))
      {
        return false;
      }
    {
      u8 cfm[28] = {}; // struct mm_version_cfm
      if (!ask (MM_VERSION_REQ, aic8800::TASK_MM, nullptr, 0, MM_VERSION_CFM,
                cfm, sizeof (cfm)))
        {
          return false;
        }
      kprintf ("[wlan] lmac 0x%08x machw 0x%08x phy 0x%08x features 0x%08x, "
               "%u sta, %u vif\n",
               static_cast<unsigned> (get32 (cfm + 0)),
               static_cast<unsigned> (get32 (cfm + 4)),
               static_cast<unsigned> (get32 (cfm + 12)),
               static_cast<unsigned> (get32 (cfm + 20)),
               static_cast<unsigned> (get16 (cfm + 24)),
               static_cast<unsigned> (cfm[26]));
    }
    // rwnx_send_me_config_req(): HT only (one stream, 20 MHz, short GI, LDPC
    // as the driver always sets it).  VHT/HE are left off: the test needs a
    // link, not a rate.
    {
      u8 req[112] = {};           // struct me_config_req
      put16 (req + 0, 0x0021u);   // ht_cap.ht_capa_info: LDPC | SGI20
      req[2] = 0x1f;              // ht_cap.a_mpdu_param: 64K, 16 us
      req[3 + 0] = 0xff;          // ht_cap.mcs_rate: MCS 0..7
      req[3 + 10] = 65;           //   rx_highest = 65 Mb/s
      req[3 + 12] = 0x01;         //   tx_params: TX MCS set defined
      put16 (req + 100, 1000);    // tx_lft (RWNX_TX_LIFETIME_MS)
      req[102] = 0;               // phy_bw_max: PHY_CHNL_BW_20
      req[103] = 1;               // ht_supp
      // vht_supp @104, he_supp @105, he_ul_on @106 = 0
      req[107] = 0;               // ps_on: no power save, the radio is polled
      // ant_div_on @108, dpsm @109 = 0
      if (!ask (ME_CONFIG_REQ, aic8800::TASK_ME, req, sizeof (req),
                ME_CONFIG_CFM))
        {
          return false;
        }
    }
    // rwnx_send_me_chan_config_req(): struct mac_chan_def { freq @0, band
    // @2, flags @3, tx_power @4 }, 6 bytes; chan2G4[14] @0, chan5G[28] @84,
    // the two counts @252 and @253.
    {
      u8 req[254] = {};
      unsigned n24 = 0, n5 = 0;
      for (const channel& c : channels)
        {
          u8* e = (c.band == PHY_BAND_2G4) ? req + 6u * n24++
                                           : req + 84u + 6u * n5++;
          put16 (e, c.freq);
          e[2] = c.band;
          e[3] = c.flags;
          e[4] = static_cast<u8> (channel_tx_power);
        }
      req[252] = static_cast<u8> (n24);
      req[253] = static_cast<u8> (n5);
      if (!ask (ME_CHAN_CONFIG_REQ, aic8800::TASK_ME, req, sizeof (req),
                ME_CHAN_CONFIG_CFM))
        {
          return false;
        }
    }
    // rwnx_open(): rwnx_send_start() ...
    {
      u8 req[72] = {};          // struct mm_start_req; phy_cfg @0 = 0
      put32 (req + 64, 300);    // uapsd_timeout
      put16 (req + 68, 20);     // lp_clk_accuracy (ppm)
      if (!ask (MM_START_REQ, aic8800::TASK_MM, req, sizeof (req),
                MM_START_CFM))
        {
          return false;
        }
    }
    // ... and rwnx_send_add_if (station).
    {
      u8 req[10] = {};          // struct mm_add_if_req
      req[0] = MM_STA;          // type
      std::memcpy (req + 2, own_address.data (), 6); // addr
      // p2p @8 = 0
      u8 cfm[2] = {};           // { status, inst_nbr }
      if (!ask (MM_ADD_IF_REQ, aic8800::TASK_MM, req, sizeof (req),
                MM_ADD_IF_CFM, cfm, sizeof (cfm)))
        {
          return false;
        }
      if (cfm[0] != 0)
        {
          kprintf ("[wlan] MM_ADD_IF status %u\n",
                   static_cast<unsigned> (cfm[0]));
          return false;
        }
      vif_index = cfm[1];
    }
    return true;
  }

  // ---- part 2: scan --------------------------------------------------------

  struct rsn_info
  {
    bool present{ false };
    bool group_ccmp{ false };
    bool group_tkip{ false };
    bool pairwise_ccmp{ false };
    bool akm_psk{ false };
    bool akm_other{ false };
    bool pmf_required{ false };
  };

  // RSN element body: version, group suite, pairwise list, AKM list, caps.
  rsn_info
  parse_rsn (const u8* body, std::size_t len)
  {
    rsn_info info{};
    if (len < 8 || get16 (body) != 1)
      {
        return info;
      }
    info.present = true;
    const auto suite = [] (const u8* s) -> int {
      return (s[0] == 0x00 && s[1] == 0x0f && s[2] == 0xac) ? s[3] : -1;
    };
    info.group_ccmp = suite (body + 2) == 4;
    info.group_tkip = suite (body + 2) == 2;
    std::size_t at = 6;
    unsigned n = get16 (body + at);
    at += 2;
    for (unsigned i = 0; i < n && at + 4 <= len; ++i, at += 4)
      {
        if (suite (body + at) == 4)
          {
            info.pairwise_ccmp = true;
          }
      }
    if (at + 2 > len)
      {
        return info;
      }
    n = get16 (body + at);
    at += 2;
    for (unsigned i = 0; i < n && at + 4 <= len; ++i, at += 4)
      {
        if (suite (body + at) == 2)
          {
            info.akm_psk = true;
          }
        else
          {
            info.akm_other = true;
          }
      }
    if (at + 2 <= len)
      {
        info.pmf_required = (get16 (body + at) & 0x0040u) != 0;
      }
    return info;
  }

  // One beacon / probe response from SCANU_RESULT_IND.
  void
  record_bss (const u8* frame, std::size_t length, std::int8_t rssi, u16 freq)
  {
    constexpr std::size_t mgmt_header = 24;
    if (length < mgmt_header + 12)
      {
        return;
      }
    const u8* fixed = frame + mgmt_header;
    wlan::bss entry{};
    std::memcpy (entry.bssid.data (), frame + 16, 6);
    entry.beacon_interval = get16 (fixed + 8);
    entry.capability = get16 (fixed + 10);
    entry.frequency = freq;
    entry.channel = static_cast<u8> (channel_of (freq));
    entry.rssi = rssi;

    rsn_info rsn{};
    bool wpa1 = false;
    std::size_t at = mgmt_header + 12;
    while (at + 2 <= length)
      {
        const u8 id = frame[at];
        const std::size_t len = frame[at + 1];
        const u8* body = frame + at + 2;
        if (at + 2 + len > length)
          {
            break;
          }
        if (id == 0 && len <= 32)
          {
            entry.ssid_length = static_cast<u8> (len);
            std::memcpy (entry.ssid.data (), body, len);
          }
        else if (id == 48)
          {
            rsn = parse_rsn (body, len);
          }
        else if (id == 221 && len >= 4 && body[0] == 0x00 && body[1] == 0x50
                 && body[2] == 0xf2 && body[3] == 0x01)
          {
            wpa1 = true;
          }
        at += 2 + len;
      }

    constexpr u16 cap_privacy = 0x0010;
    if ((entry.capability & cap_privacy) == 0)
      {
        entry.sec = wlan::security::open;
        entry.security_text = "open";
      }
    else if (rsn.present)
      {
        entry.sec = wlan::security::unsupported;
        if (rsn.pmf_required)
          {
            entry.security_text = "WPA2/3 PMF-required";
          }
        else if (!rsn.akm_psk)
          {
            entry.security_text = rsn.akm_other ? "WPA3/802.1X" : "RSN ?";
          }
        else if (!rsn.pairwise_ccmp)
          {
            entry.security_text = "WPA2 TKIP-only";
          }
        else if (!rsn.group_ccmp && !rsn.group_tkip)
          {
            entry.security_text = "WPA2 (unknown group cipher)";
          }
        else
          {
            entry.sec = wlan::security::wpa2_psk;
            entry.group_tkip = rsn.group_tkip;
            entry.security_text = rsn.group_ccmp ? "WPA2-PSK"
                                                 : "WPA2-PSK (TKIP group)";
          }
      }
    else
      {
        entry.sec = wlan::security::unsupported;
        entry.security_text = wpa1 ? "WPA (TKIP)" : "WEP";
      }

    // Keep the strongest observation per BSSID; a hidden-SSID beacon does
    // not erase a name a probe response gave.
    for (std::size_t i = 0; i < bss_count; ++i)
      {
        if (bss_table[i].bssid == entry.bssid)
          {
            if (entry.ssid_length == 0)
              {
                entry.ssid = bss_table[i].ssid;
                entry.ssid_length = bss_table[i].ssid_length;
              }
            if (entry.rssi >= bss_table[i].rssi
                || bss_table[i].ssid_length == 0)
              {
                bss_table[i] = entry;
              }
            return;
          }
      }
    if (bss_count < bss_table.size ())
      {
        bss_table[bss_count++] = entry;
      }
  }

  // Inside aic8800::poll(): record, never ask.
  void
  on_message (u16 id, const u8* param, u16 len)
  {
    switch (id)
      {
      case SCANU_RESULT_IND:
        // struct scanu_result_ind: length @0, framectrl @2, center_freq @4,
        // band @6, sta_idx @7, inst_nbr @8, rssi @9, payload @12.
        if (len >= 12)
          {
            std::size_t n = get16 (param);
            if (n > static_cast<std::size_t> (len - 12))
              {
                n = len - 12u;
              }
            record_bss (param + 12, n, static_cast<std::int8_t> (param[9]),
                        get16 (param + 4));
          }
        break;
      case SCANU_START_CFM:
        scan_done = true;
        break;
      case SM_CONNECT_IND:
        // struct sm_connect_ind: status_code @0, bssid @2, vif_idx @9,
        // ap_idx @10, qos @12.
        if (len >= 14)
          {
            connect_status = get16 (param);
            ap_index = param[10];
            ap_qos = param[12] != 0;
            connect_event = true;
          }
        break;
      case SM_DISCONNECT_IND:
        // struct sm_disconnect_ind: reason_code @0, vif_idx @2.
        disconnect_reason = len >= 2 ? get16 (param) : 0;
        disconnect_event = true;
        break;
      default:
        ++stat.indications;
        break;
      }
  }

  // ---- data ------------------------------------------------------------------

  // LLC/SNAP + payload -> Ethernet II, or the EAPOL queue.
  void
  deliver (const u8* da, const u8* sa, const u8* p, std::size_t n)
  {
    constexpr u8 snap[6] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
    if (n < 8 || std::memcmp (p, snap, 6) != 0)
      {
        ++stat.rx_dropped;
        return;
      }
    const u8* type = p + 6;
    const u8* payload = p + 8;
    const std::size_t length = n - 8;
    if (type[0] == ethertype_eapol[0] && type[1] == ethertype_eapol[1])
      {
        ++stat.rx_eapol;
        const unsigned next = (eapol_tail + 1u) % 4u;
        if (next != eapol_head && length <= sizeof (eapol_queue[0].data))
          {
            eapol_queue[eapol_tail].length = static_cast<u16> (length);
            std::memcpy (eapol_queue[eapol_tail].data, payload, length);
            eapol_tail = next;
          }
        return;
      }
    if (!port_open || on_receive == nullptr || 14 + length > sizeof (eth_buf))
      {
        ++stat.rx_dropped;
        return;
      }
    std::memcpy (eth_buf, da, 6);
    std::memcpy (eth_buf + 6, sa, 6);
    eth_buf[12] = type[0];
    eth_buf[13] = type[1];
    std::memcpy (eth_buf + 14, payload, length);
    ++stat.rx_delivered;
    on_receive ({ eth_buf, 14 + length });
  }

  // rwnx_rxdataind_aicwf(): the firmware hands up the 802.11 MPDU, already
  // decrypted but with the cipher header still in place, behind a 60-byte
  // receive header (struct hw_rxhdr + 2 + 2 alignment):
  //
  //   @0   hwvect.len (16 bits): the MPDU length
  //   @36  hwvect status word; decr_status = bits 4..2
  //   @60  the MPDU
  void
  on_data (const u8* frame, u32 total)
  {
    ++stat.rx_data;
    if (total < aic8800::rx_header + 24u)
      {
        ++stat.rx_dropped;
        return;
      }
    const unsigned decr = (get32 (frame + 36) >> 2) & 7u;
    const u8* m = frame + aic8800::rx_header;
    const std::size_t mlen = total - aic8800::rx_header;
    // Data frames that carry a body only (type 10, subtype bit 2 clear).
    if ((m[0] & 0x0cu) != 0x08u || (m[0] & 0x40u) != 0)
      {
        ++stat.rx_dropped;
        return;
      }
    std::size_t hdr = 24;
    const bool qos = (m[0] & 0x80u) != 0;
    if (qos)
      {
        hdr += 2;
      }
    if (m[1] & 0x80u) // +HTC
      {
        hdr += 4;
      }
    const u8* da;
    const u8* sa;
    switch (m[1] & 0x03u)
      {
      case 0x02: // from the distribution system: the AP to us
        da = m + 4;
        sa = m + 16;
        break;
      case 0x01: // to the distribution system
        da = m + 16;
        sa = m + 10;
        break;
      case 0x00:
        da = m + 4;
        sa = m + 10;
        break;
      default: // four addresses: not a station's traffic
        ++stat.rx_dropped;
        return;
      }
    // RWNX_RX_HD_DECR_*: 1 WEP (4-byte header), 2 TKIP, 3..6 CCMP/GCMP (8).
    std::size_t iv = 0;
    if (decr >= 2 && decr <= 6)
      {
        iv = 8;
      }
    else if (decr == 1)
      {
        iv = 4;
      }
    if (mlen < hdr + iv + 8)
      {
        ++stat.rx_dropped;
        return;
      }
    const u8* body = m + hdr + iv;
    std::size_t left = mlen - hdr - iv;
    const bool amsdu = qos && (m[24] & 0x80u) != 0;
    if (!amsdu)
      {
        deliver (da, sa, body, left);
        return;
      }
    // A-MSDU: { DA, SA, length (big endian), LLC/SNAP + payload }, each
    // subframe padded to four bytes.
    while (left >= 14 + 8)
      {
        const std::size_t sub = get16be (body + 12);
        if (14 + sub > left)
          {
            break;
          }
        deliver (body, body + 6, body + 14, sub);
        const std::size_t step = (14 + sub + 3u) & ~static_cast<std::size_t> (3);
        if (step >= left)
          {
            break;
          }
        body += step;
        left -= step;
      }
  }

  bool
  send_frame (const u8* dst, const u8* type, const u8* payload,
              std::size_t length)
  {
    const bool ok = aic8800::send_data (dst, own_address.data (), type, payload,
                                        static_cast<u32> (length), vif_index,
                                        ap_index, ap_qos ? 0 : 0xFF);
    if (ok)
      {
        ++stat.tx_data;
      }
    else
      {
        ++stat.tx_errors;
      }
    return ok;
  }

  // ---- part 3: WPA2 ----------------------------------------------------------
  constexpr std::size_t eapol_key_fixed = 99;
  constexpr u16 key_info_version = 0x0007;
  constexpr u16 key_info_pairwise = 0x0008;
  constexpr u16 key_info_install = 0x0040;
  constexpr u16 key_info_ack = 0x0080;
  constexpr u16 key_info_mic = 0x0100;
  constexpr u16 key_info_secure = 0x0200;
  constexpr u16 key_info_encrypted = 0x1000;

  // No hardware RNG is used: hash the timer, the addresses, the counters and
  // the AP's nonce.  Unique per handshake, which is what a nonce has to be.
  void
  make_snonce (void)
  {
    crypto::sha1 h;
    const u64 t = clint::now ();
    ++nonce_counter;
    h.update ({ reinterpret_cast<const u8*> (&t), sizeof (t) });
    h.update ({ reinterpret_cast<const u8*> (&nonce_counter),
                sizeof (nonce_counter) });
    h.update (own_address);
    h.update (target.bssid);
    h.update ({ reinterpret_cast<const u8*> (&stat), sizeof (stat) });
    h.update ({ reinterpret_cast<const u8*> (&aic8800::stats ()),
                sizeof (aic8800::counters) });
    const auto first = h.finish ();
    crypto::sha1 h2;
    h2.update (first);
    h2.update (anonce);
    const auto second = h2.finish ();
    std::memcpy (snonce.data (), first.data (), 20);
    std::memcpy (snonce.data () + 20, second.data (), 12);
  }

  void
  derive_ptk (void)
  {
    std::array<u8, 76> data{};
    const u8* aa = target.bssid.data ();
    const u8* spa = own_address.data ();
    const bool aa_first = std::memcmp (aa, spa, 6) < 0;
    std::memcpy (data.data (), aa_first ? aa : spa, 6);
    std::memcpy (data.data () + 6, aa_first ? spa : aa, 6);
    const bool an_first = std::memcmp (anonce.data (), snonce.data (), 32) < 0;
    std::memcpy (data.data () + 12,
                 an_first ? anonce.data () : snonce.data (), 32);
    std::memcpy (data.data () + 44,
                 an_first ? snonce.data () : anonce.data (), 32);
    constexpr std::string_view label{ "Pairwise key expansion" };
    crypto::prf_sha1 (pmk,
                      { reinterpret_cast<const u8*> (label.data ()),
                        label.size () },
                      data, ptk);
    ptk_derived = true;
  }

  void
  eapol_mic (const u8* frame, std::size_t length, u8* out)
  {
    crypto::hmac_sha1 h{ { ptk.data (), 16 } };
    h.update ({ frame, length });
    const auto digest = h.finish ();
    std::memcpy (out, digest.data (), 16);
  }

  bool
  send_eapol_key (u16 key_info, u8 version, u64 replay, const u8* nonce,
                  std::span<const u8> key_data)
  {
    std::array<u8, eapol_key_fixed + 64> frame{};
    const std::size_t length = eapol_key_fixed + key_data.size ();
    frame[0] = version;
    frame[1] = 3; // EAPOL-Key
    put16be (frame.data () + 2, static_cast<u16> (length - 4));
    frame[4] = 2; // RSN key descriptor
    put16be (frame.data () + 5, key_info);
    put16be (frame.data () + 7, 0);
    for (int i = 0; i < 8; ++i)
      {
        frame[9 + i] = static_cast<u8> (replay >> (8 * (7 - i)));
      }
    if (nonce != nullptr)
      {
        std::memcpy (frame.data () + 17, nonce, 32);
      }
    put16be (frame.data () + 97, static_cast<u16> (key_data.size ()));
    std::memcpy (frame.data () + eapol_key_fixed, key_data.data (),
                 key_data.size ());
    eapol_mic (frame.data (), length, frame.data () + 81);
    return send_frame (target.bssid.data (), ethertype_eapol, frame.data (),
                       length);
  }

  // rwnx_send_key_add(): struct mm_key_add_req { key_idx @0, sta_idx @1,
  // key.length @4, key.array @8, cipher_suite @40, inst_nbr @41, spp @42,
  // pairwise @43 }.
  bool
  install_key (bool pairwise, u8 key_id, u8 cipher, const u8* key,
               std::size_t key_len)
  {
    u8 req[44] = {};
    req[0] = pairwise ? 0 : key_id;
    req[1] = pairwise ? ap_index : INVALID_STA;
    req[4] = static_cast<u8> (key_len);
    std::memcpy (req + 8, key, key_len);
    req[40] = cipher;
    req[41] = vif_index;
    req[43] = pairwise ? 1 : 0;
    u8 cfm[4] = {}; // { status, hw_key_idx }
    if (!ask (MM_KEY_ADD_REQ, aic8800::TASK_MM, req, sizeof (req),
              MM_KEY_ADD_CFM, cfm, sizeof (cfm)))
      {
        return false;
      }
    if (cfm[0] != 0)
      {
        kprintf ("[wlan] MM_KEY_ADD (%s) status %u\n",
                 pairwise ? "pairwise" : "group",
                 static_cast<unsigned> (cfm[0]));
        return false;
      }
    return true;
  }

  // Find the GTK KDE (dd len 00-0f-ac 01 keyid rsvd key...) in the decrypted
  // key data and give the key to the firmware.
  bool
  install_gtk (const u8* data, std::size_t length)
  {
    std::size_t at = 0;
    while (at + 2 <= length)
      {
        const u8 id = data[at];
        const std::size_t len = data[at + 1];
        if (id == 0xdd && len == 0)
          {
            break; // padding
          }
        if (at + 2 + len > length)
          {
            break;
          }
        const u8* body = data + at + 2;
        if (id == 0xdd && len >= 6 + 16 && body[0] == 0x00 && body[1] == 0x0f
            && body[2] == 0xac && body[3] == 0x01)
          {
            const u8 key_id = body[4] & 0x03u;
            const std::size_t key_len = len - 6;
            if (key_len == 16)
              {
                if (!install_key (false, key_id, MAC_CIPHER_CCMP, body + 6, 16))
                  {
                    return false;
                  }
              }
            else if (key_len == 32)
              {
                // TKIP: the KDE carries TK | AP's TX MIC | AP's RX MIC; a
                // driver takes TK | its TX MIC | its RX MIC, so the two MIC
                // keys change places (wpa_supplicant_install_gtk()).
                u8 k[32];
                std::memcpy (k, body + 6, 16);
                std::memcpy (k + 16, body + 6 + 24, 8);
                std::memcpy (k + 24, body + 6 + 16, 8);
                if (!install_key (false, key_id, MAC_CIPHER_TKIP, k, 32))
                  {
                    return false;
                  }
              }
            else
              {
                kprintf ("[wlan] GTK of %u bytes: unsupported\n",
                         static_cast<unsigned> (key_len));
                return false;
              }
            kprintf ("[wlan] group key %u installed (%s)\n",
                     static_cast<unsigned> (key_id),
                     key_len == 16 ? "CCMP" : "TKIP");
            return true;
          }
        at += 2 + len;
      }
    return false;
  }

  // Top level only (it asks the firmware to install keys).
  void
  handle_eapol (const u8* frame, std::size_t length)
  {
    if (target.sec != wlan::security::wpa2_psk || length < eapol_key_fixed
        || frame[1] != 3)
      {
        return;
      }
    const std::size_t eapol_length = 4u + get16be (frame + 2);
    if (eapol_length > length || eapol_length < eapol_key_fixed
        || frame[4] != 2)
      {
        return;
      }
    const u16 key_info = get16be (frame + 5);
    const u8 version = frame[0];
    const std::size_t key_data_length = get16be (frame + 97);
    if (eapol_key_fixed + key_data_length > eapol_length)
      {
        return;
      }
    if ((key_info & key_info_version) != 2)
      {
        kprintf ("[wlan] EAPOL key descriptor version %u is not supported "
                 "(only HMAC-SHA1/AES)\n",
                 static_cast<unsigned> (key_info & key_info_version));
        handshake_failed = true;
        return;
      }
    u64 replay = 0;
    for (int i = 0; i < 8; ++i)
      {
        replay = (replay << 8) | frame[9 + i];
      }
    if (replay_valid && replay <= replay_counter
        && (key_info & key_info_mic) != 0)
      {
        return; // replayed or retransmitted message 3
      }

    if ((key_info & key_info_mic) == 0)
      {
        // Message 1 of 4: the AP's nonce.
        if ((key_info & (key_info_pairwise | key_info_ack))
            != (key_info_pairwise | key_info_ack))
          {
            return;
          }
        std::memcpy (anonce.data (), frame + 17, 32);
        make_snonce ();
        derive_ptk ();
        kprintf ("[wlan] 4-way handshake: 1/4 received, sending 2/4\n");
        const std::span<const u8> element
            = target.group_tkip ? std::span<const u8> (rsn_element_tkip)
                                : std::span<const u8> (rsn_element_ccmp);
        (void)send_eapol_key (static_cast<u16> (2 | key_info_pairwise
                                                | key_info_mic),
                              version, replay, snonce.data (), element);
        return;
      }

    // Messages with a MIC: verify it over the frame with the MIC zeroed.
    if (!ptk_derived)
      {
        return;
      }
    static std::array<u8, 640> copy;
    if (eapol_length > copy.size ())
      {
        return;
      }
    std::memcpy (copy.data (), frame, eapol_length);
    std::memset (copy.data () + 81, 0, 16);
    std::array<u8, 16> mic{};
    eapol_mic (copy.data (), eapol_length, mic.data ());
    if (std::memcmp (mic.data (), frame + 81, 16) != 0)
      {
        kprintf ("[wlan] EAPOL MIC mismatch - wrong passphrase?\n");
        handshake_failed = true;
        return;
      }
    replay_counter = replay;
    replay_valid = true;

    static std::array<u8, 512> key_data;
    std::size_t plain_length = 0;
    if ((key_info & key_info_encrypted) != 0 && key_data_length >= 16)
      {
        if (key_data_length - 8 > key_data.size ())
          {
            return;
          }
        const crypto::aes128 kek{ { ptk.data () + 16, 16 } };
        plain_length = key_data_length - 8;
        if (!crypto::aes_unwrap (kek,
                                 { frame + eapol_key_fixed, key_data_length },
                                 { key_data.data (), plain_length }))
          {
            kprintf ("[wlan] EAPOL key data unwrap failed\n");
            handshake_failed = true;
            return;
          }
      }

    if ((key_info & key_info_pairwise) != 0)
      {
        // Message 3 of 4.
        if ((key_info & key_info_install) == 0
            || std::memcmp (anonce.data (), frame + 17, 32) != 0)
          {
            return;
          }
        kprintf ("[wlan] 4-way handshake: 3/4 verified, sending 4/4\n");
        (void)send_eapol_key (static_cast<u16> (2 | key_info_pairwise
                                                | key_info_mic
                                                | key_info_secure),
                              version, replay, nullptr, {});
        // Message 4 must leave in the clear: give the firmware time to put
        // it on the air before the pairwise key exists (wpa_supplicant
        // relies on the same order: send 4/4, then set the keys).
        for (unsigned i = 0; i < 10u; ++i)
          {
            (void)aic8800::poll ();
            wifi_platform::sleep_ms (2);
          }
        if (!install_key (true, 0, MAC_CIPHER_CCMP, ptk.data () + 32, 16))
          {
            handshake_failed = true;
            return;
          }
        if (!install_gtk (key_data.data (), plain_length))
          {
            kprintf ("[wlan] 4-way handshake: no usable GTK in message 3\n");
            handshake_failed = true;
            return;
          }
        // rwnx_send_me_set_control_port_req (opened, sta): what Linux does
        // when the supplicant marks the station authorized.
        const u8 req[2] = { ap_index, 1 };
        if (!ask (ME_SET_CONTROL_PORT_REQ, aic8800::TASK_ME, req, sizeof (req),
                  ME_SET_CONTROL_PORT_CFM))
          {
            handshake_failed = true;
            return;
          }
        handshake_done = true;
      }
    else
      {
        // Group key handshake, message 1 of 2.
        if (!install_gtk (key_data.data (), plain_length))
          {
            return;
          }
        kprintf ("[wlan] group key updated\n");
        (void)send_eapol_key (static_cast<u16> (2 | key_info_mic
                                                | key_info_secure),
                              version, replay, nullptr, {});
      }
  }

  void
  link_down (const char* why)
  {
    const bool was = (state == phase::linked);
    state = phase::idle;
    port_open = false;
    ptk_derived = false;
    handshake_done = false;
    ap_index = INVALID_STA;
    if (why != nullptr)
      {
        kprintf ("[wlan] link down: %s\n", why);
      }
    if (was && on_link != nullptr)
      {
        on_link (false);
      }
  }

  // The work poll() does besides receiving; also the body of every wait.
  void
  service (void)
  {
    (void)aic8800::poll ();
    while (eapol_head != eapol_tail)
      {
        static eapol_slot slot;
        slot = eapol_queue[eapol_head];
        eapol_head = (eapol_head + 1u) % 4u;
        handle_eapol (slot.data, slot.length);
      }
  }

  template <typename Predicate>
  bool
  wait_for (u32 timeout_ms, Predicate done)
  {
    const u32 start = wifi_platform::millis ();
    for (;;)
      {
        service ();
        if (done ())
          {
            return true;
          }
        if (wifi_platform::millis () - start >= timeout_ms)
          {
            return false;
          }
        wifi_platform::sleep_ms (2);
      }
  }

  // rwnx_send_scanu_req(): struct scanu_start_req { chan[42] @0 (6 bytes
  // each: freq, band, flags, tx_power), ssid[3] @252 (33 bytes each: length,
  // text), bssid @352, add_ies @360, add_ie_len @364, vif_idx @366, chan_cnt
  // @367, ssid_cnt @368, no_cck @369, duration @372 }.
  bool
  run_scan (std::string_view ssid)
  {
    u8 req[376] = {};
    unsigned n = 0;
    for (const channel& c : channels)
      {
        u8* e = req + 6u * n++;
        put16 (e, c.freq);
        e[2] = c.band;
        e[3] = c.flags;
        e[4] = static_cast<u8> (channel_tx_power);
      }
    // One SSID entry: the wildcard (length 0) or the network looked for.
    req[252] = static_cast<u8> (ssid.size ());
    std::memcpy (req + 253, ssid.data (), ssid.size ());
    std::memcpy (req + 352, broadcast, 6);
    req[366] = vif_index;
    req[367] = static_cast<u8> (n);
    req[368] = 1;
    scan_done = false;
    if (!ask (SCANU_START_REQ, aic8800::TASK_SCANU, req, sizeof (req),
              SCANU_START_CFM_ADDTIONAL))
      {
        return false;
      }
    if (!wait_for (20000, [] { return scan_done; }))
      {
        kprintf ("[wlan] scan: no SCANU_START_CFM within 20 s\n");
        return false;
      }
    return true;
  }

  const wlan::bss*
  find (std::string_view ssid)
  {
    const wlan::bss* best = nullptr;
    for (std::size_t i = 0; i < bss_count; ++i)
      {
        if (bss_table[i].name () == ssid
            && (best == nullptr || bss_table[i].rssi > best->rssi))
          {
            best = &bss_table[i];
          }
      }
    return best;
  }
} // namespace

namespace wlan
{
  bool
  started () noexcept
  {
    return chip_started;
  }

  const mac_address&
  address () noexcept
  {
    return own_address;
  }

  bool
  linked () noexcept
  {
    return state == phase::linked;
  }

  const bss&
  current () noexcept
  {
    return target;
  }

  const counters&
  stats () noexcept
  {
    return stat;
  }

  void
  set_receive_handler (receive_handler handler) noexcept
  {
    on_receive = handler;
  }

  void
  set_link_handler (link_handler handler) noexcept
  {
    on_link = handler;
  }

  bool
  start () noexcept
  {
    if (chip_started)
      {
        return true;
      }
    aic8800::set_message_handler (on_message);
    aic8800::set_data_handler (on_data);
    const bool booted = aic8800::boot (chip);
    const sdio1::info& s = chip.sdio;
    kprintf ("[sdio] mmc1 VERID 0x%08x, clocks ahb 0x%08x card 0x%08x, "
             "WL_REG_ON active %s\n",
             static_cast<unsigned> (s.verid), static_cast<unsigned> (s.clk_ahb),
             static_cast<unsigned> (s.clk_card),
             s.reg_on_high ? "high" : "low");
    kprintf ("[sdio] OCR 0x%08x, RCA 0x%04x, %u function(s), CCCR rev 0x%02x, "
             "id %04x:%04x (function 1 %04x:%04x), %u Hz 4-bit\n",
             static_cast<unsigned> (s.ocr), static_cast<unsigned> (s.rca),
             static_cast<unsigned> (s.functions),
             static_cast<unsigned> (s.cccr_rev),
             static_cast<unsigned> (s.vendor), static_cast<unsigned> (s.device),
             static_cast<unsigned> (s.f1_vendor),
             static_cast<unsigned> (s.f1_device),
             static_cast<unsigned> (s.card_hz));
    if (!booted)
      {
        kprintf ("[aic] boot failed: %s\n", aic8800::last_error ());
        return false;
      }
    kprintf ("[aic] AIC8800D80/D40, chip id word 0x%08x (revision %u%s), "
             "firmware %u bytes (image version 0x%08x), boot status 0x%08x\n",
             static_cast<unsigned> (chip.chip_id_word),
             static_cast<unsigned> (chip.revision), chip.id_h ? ", H" : "",
             static_cast<unsigned> (chip.fw_size),
             static_cast<unsigned> (chip.fw_rom_version),
             static_cast<unsigned> (chip.boot_status));
    if (!configure ())
      {
        kprintf ("[wlan] firmware configuration failed\n");
        return false;
      }
    kprintf ("[wlan] station %02x:%02x:%02x:%02x:%02x:%02x, interface %u\n",
             own_address[0], own_address[1], own_address[2], own_address[3],
             own_address[4], own_address[5], static_cast<unsigned> (vif_index));
    chip_started = true;
    return true;
  }

  std::span<const bss>
  scan () noexcept
  {
    bss_count = 0;
    if (chip_started && state == phase::idle)
      {
        (void)run_scan ({});
      }
    return { bss_table.data (), bss_count };
  }

  void
  leave () noexcept
  {
    if (!chip_started || state == phase::idle)
      {
        return;
      }
    // rwnx_send_sm_disconnect_req(): { reason_code @0, vif_idx @2 }.
    u8 req[4] = {};
    put16 (req, 3); // deauthenticated because the station is leaving
    req[2] = vif_index;
    (void)ask (SM_DISCONNECT_REQ, aic8800::TASK_SM, req, sizeof (req),
               SM_DISCONNECT_CFM);
    link_down ("left");
    disconnect_event = false;
  }

  bool
  join (std::string_view ssid, std::string_view passphrase) noexcept
  {
    if (!chip_started || ssid.empty () || ssid.size () > 32)
      {
        return false;
      }
    if (state != phase::idle)
      {
        leave ();
      }

    // 1. the network
    const bss* found = find (ssid);
    if (found == nullptr)
      {
        (void)run_scan (ssid);
        found = find (ssid);
      }
    if (found == nullptr)
      {
        kprintf ("[wlan] \"%.*s\" not found\n",
                 static_cast<int> (ssid.size ()), ssid.data ());
        return false;
      }
    target = *found;
    const bool secure = !passphrase.empty ();
    if (target.sec == security::unsupported
        || (target.sec == security::wpa2_psk) != secure)
      {
        kprintf ("[wlan] \"%.*s\" is %s: %s\n",
                 static_cast<int> (ssid.size ()), ssid.data (),
                 target.security_text,
                 target.sec == security::unsupported
                     ? "not supported"
                     : (secure ? "it takes no passphrase"
                               : "it needs a passphrase"));
        return false;
      }
    kprintf ("[wlan] joining \"%.*s\" %02x:%02x:%02x:%02x:%02x:%02x, channel "
             "%u (%u MHz), %d dBm, %s\n",
             static_cast<int> (ssid.size ()), ssid.data (), target.bssid[0],
             target.bssid[1], target.bssid[2], target.bssid[3],
             target.bssid[4], target.bssid[5],
             static_cast<unsigned> (target.channel),
             static_cast<unsigned> (target.frequency),
             static_cast<int> (target.rssi), target.security_text);

    // 2. the pairwise master key (4096 rounds of HMAC-SHA1, done once)
    if (secure)
      {
        if (passphrase.size () < 8 || passphrase.size () > 63)
          {
            kprintf ("[wlan] a WPA2 passphrase has 8..63 characters\n");
            return false;
          }
        crypto::pbkdf2_sha1 (
            { reinterpret_cast<const u8*> (passphrase.data ()),
              passphrase.size () },
            { reinterpret_cast<const u8*> (ssid.data ()), ssid.size () }, 4096,
            pmk);
      }
    ptk_derived = false;
    handshake_done = false;
    handshake_failed = false;
    replay_valid = false;
    port_open = false;
    eapol_head = eapol_tail = 0;
    connect_event = false;
    disconnect_event = false;

    // 3. rwnx_send_sm_connect_req(): struct sm_connect_req { ssid @0 (length,
    //    text), bssid @34, chan @40 (freq, band, flags), flags @48,
    //    ctrl_port_ethertype @52, ie_len @54, listen_interval @56,
    //    dont_wait_bcmc @58, auth_type @59, uapsd_queues @60, vif_idx @61,
    //    ie_buf @64 }.  The firmware authenticates (open system) and
    //    associates; with CONTROL_PORT_HOST the EAPOL frames are the host's.
    {
      u8 req[320] = {};
      req[0] = static_cast<u8> (ssid.size ());
      std::memcpy (req + 1, ssid.data (), ssid.size ());
      std::memcpy (req + 34, target.bssid.data (), 6);
      put16 (req + 40, target.frequency);
      req[42] = target.frequency >= 5000 ? PHY_BAND_5G : PHY_BAND_2G4;
      if (secure)
        {
          put32 (req + 48, CONTROL_PORT_HOST | WPA_WPA2_IN_USE);
          req[52] = ethertype_eapol[0];
          req[53] = ethertype_eapol[1];
          const std::span<const u8> element
              = target.group_tkip ? std::span<const u8> (rsn_element_tkip)
                                  : std::span<const u8> (rsn_element_ccmp);
          put16 (req + 54, static_cast<u16> (element.size ()));
          std::memcpy (req + 64, element.data (), element.size ());
        }
      req[61] = vif_index;
      u8 cfm[1] = { 0xFF };
      state = phase::connecting;
      if (!ask (SM_CONNECT_REQ, aic8800::TASK_SM, req, sizeof (req),
                SM_CONNECT_CFM, cfm, sizeof (cfm)))
        {
          state = phase::idle;
          return false;
        }
      if (cfm[0] != 0)
        {
          kprintf ("[wlan] SM_CONNECT_REQ refused, status %u\n",
                   static_cast<unsigned> (cfm[0]));
          state = phase::idle;
          return false;
        }
    }
    if (!wait_for (10000, [] { return connect_event; }))
      {
        kprintf ("[wlan] no answer from the access point within 10 s\n");
        state = phase::idle;
        return false;
      }
    if (connect_status != 0)
      {
        kprintf ("[wlan] association failed, 802.11 status %u\n",
                 static_cast<unsigned> (connect_status));
        state = phase::idle;
        return false;
      }
    kprintf ("[wlan] associated (station index %u, %s)\n",
             static_cast<unsigned> (ap_index), ap_qos ? "QoS" : "no QoS");

    // 4. WPA2: the AP starts the 4-way handshake
    if (secure)
      {
        state = phase::handshake;
        const bool finished = wait_for (10000, [] {
          return handshake_done || handshake_failed || disconnect_event;
        });
        if (!finished || !handshake_done)
          {
            if (disconnect_event)
              {
                kprintf ("[wlan] disconnected during the handshake, reason "
                         "%u (15 = wrong passphrase / handshake timeout)\n",
                         static_cast<unsigned> (disconnect_reason));
              }
            else if (!handshake_failed)
              {
                kprintf ("[wlan] 4-way handshake timed out\n");
              }
            leave ();
            return false;
          }
      }

    port_open = true;
    state = phase::linked;
    kprintf ("[wlan] link up\n");
    if (on_link != nullptr)
      {
        on_link (true);
      }
    return true;
  }

  void
  poll () noexcept
  {
    if (!chip_started)
      {
        return;
      }
    service ();
    if (disconnect_event && state != phase::idle)
      {
        disconnect_event = false;
        kprintf ("[wlan] disconnected by the access point, reason %u\n",
                 static_cast<unsigned> (disconnect_reason));
        link_down (nullptr);
      }
  }

  bool
  send_ethernet (std::span<const std::uint8_t> frame) noexcept
  {
    if (state != phase::linked || frame.size () < 14)
      {
        return false;
      }
    return send_frame (frame.data (), frame.data () + 12, frame.data () + 14,
                       frame.size () - 14);
  }
} // namespace wlan
