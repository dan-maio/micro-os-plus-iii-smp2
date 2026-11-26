/*
 * aic8800.cpp -- the AIC8800D80/D40 below the 802.11 logic (aic8800.hpp).
 *
 * Every constant and every sequence here is the vendor driver's; the
 * function named in each comment is where it comes from.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "aic8800.hpp"
#include "wifi-platform.hpp"

#include <cstdio>
#include <cstring>

// The firmware images (aic8800-fw.S).
extern "C" const std::uint8_t aic8800_fw_u02_start[];
extern "C" const std::uint8_t aic8800_fw_u02_end[];
extern "C" const std::uint8_t aic8800_fw_h_u02_start[];
extern "C" const std::uint8_t aic8800_fw_h_u02_end[];

namespace
{
  using u8 = std::uint8_t;
  using u16 = std::uint16_t;
  using u32 = std::uint32_t;

  // ---- aicsdio.c: SDIO IDs -------------------------------------------------
  constexpr u16 VENDOR_AIC8800 = 0xc8a1;
  constexpr u16 DEVICE_AIC8800D80 = 0x0082; // also the D40
  constexpr u16 DEVICE_AIC8800DC = 0xc08d;
  constexpr u16 DEVICE_AIC8800D80X2 = 0x2082;
  constexpr u16 VENDOR_AIC8801 = 0x5449;

  // ---- aicsdio.h: function 1 registers, "V3" map (D80 family) -------------
  constexpr unsigned FN = 1;
  constexpr u32 REG_INTR_ENABLE = 0x00;
  constexpr u32 REG_INTR_PENDING = 0x01;
  constexpr u32 REG_FLOW_CTRL = 0x03;
  constexpr u32 REG_MISC_INT_STATUS = 0x04;
  constexpr u32 REG_BYTEMODE_ENABLE = 0x07;
  constexpr u32 REG_RD_FIFO = 0x0F;
  constexpr u32 REG_WR_FIFO = 0x10;
  constexpr u8 SDIO_OTHER_INTERRUPT = 0x80;

  constexpr u32 BLOCK = sdio1::block_size;
  constexpr u32 BUFFER_SIZE = 1536; // one firmware buffer (flow control unit)
  constexpr u32 TAIL_LEN = 4;
  constexpr unsigned FLOW_CTRL_RETRY_COUNT = 50;
  constexpr unsigned DATA_FLOW_CTRL_THRESH = 2;

  // Frame types (byte 2 of the 4-byte SDIO header).
  constexpr u8 TYPE_DATA_TX = 0x01;
  constexpr u8 TYPE_CFG = 0x10;
  constexpr u8 TYPE_CFG_CMD_RSP = 0x11;
  constexpr u8 TYPE_CFG_PRINT = 0x13;

  // ---- aic_bsp_driver.h: boot ROM messages and memory map ------------------
  constexpr u16 DBG_MEM_READ_REQ = 0x400, DBG_MEM_READ_CFM = 0x401;
  constexpr u16 DBG_MEM_WRITE_REQ = 0x402, DBG_MEM_WRITE_CFM = 0x403;
  constexpr u16 DBG_MEM_BLOCK_WRITE_REQ = 0x40b, DBG_MEM_BLOCK_WRITE_CFM = 0x40c;
  constexpr u16 DBG_START_APP_REQ = 0x40d, DBG_START_APP_CFM = 0x40e;
  constexpr u32 HOST_START_APP_AUTO = 1;
  constexpr u32 CHIP_ID_ADDR = 0x40500000u;
  constexpr u32 RAM_FMAC_FW_ADDR = 0x00120000u;
  constexpr u8 CHIP_REV_U01 = 1, CHIP_REV_U02 = 3, CHIP_REV_U03 = 7;

  // E2A message header: id, dest, src, param_len, pattern (ipc_e2a_msg).
  constexpr u32 E2A_HEADER = 12;

  alignas (8) u8 rx_buf[112 * BLOCK]; // misc status counts up to 0x6F blocks
  alignas (8) u8 msg_buf[4 * BLOCK];
  alignas (8) u8 data_buf[4 * BLOCK];

  bool fw_running = false;
  bool bus_up = false;
  bool in_poll = false;
  bool in_request = false;
  char err[160] = "";
  aic8800::counters cnt{};
  aic8800::message_handler on_message = nullptr;
  aic8800::data_handler on_data = nullptr;

  struct
  {
    bool waiting{ false };
    bool done{ false };
    u16 id{ 0 };
    void* out{ nullptr };
    u16 max{ 0 };
  } pending;

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

  bool
  fail (const char* what)
  {
    std::snprintf (err, sizeof (err), "%s [%s]", what, sdio1::last_error ());
    return false;
  }

  // aicsdio.c crc8_ponl_107(), bit for bit (polynomial x^8 + x^2 + x + 1).
  u8
  crc8 (const u8* p, unsigned n)
  {
    u8 crc = 0;
    while (n--)
      {
        for (unsigned i = 0x80; i > 0; i /= 2)
          {
            if (crc & 0x80)
              {
                crc = static_cast<u8> (crc * 2);
                crc ^= 0x07;
              }
            else
              {
                crc = static_cast<u8> (crc * 2);
              }
            if ((*p) & i)
              {
                crc ^= 0x07;
              }
          }
        ++p;
      }
    return crc;
  }

  // aicwf_sdio_flow_ctrl(): how many firmware buffers are free.
  unsigned
  flow_ctrl (void)
  {
    for (unsigned count = 0;; ++count)
      {
        u8 fc = 0;
        if (!sdio1::read_byte (FN, REG_FLOW_CTRL, fc))
          {
            ++cnt.io_errors;
            return 0;
          }
        if (fc != 0)
          {
            return fc;
          }
        if (count >= FLOW_CTRL_RETRY_COUNT)
          {
            return 0;
          }
        if (count < 30)
          {
            sdio1::delay_us (200);
          }
        else
          {
            wifi_platform::sleep_ms (count < 40 ? 1 : 10);
          }
      }
  }

  // aicwf_sdio_func_init() / aicwf_sdiov3_func_init() + aicwf_sdio_bus_start()
  // for the D80: block size, I/O enable, the vendor register 0xF2 of function
  // 0, no byte mode, then the interrupt enables (the status registers are
  // polled; nothing here takes an interrupt).
  bool
  function_init (void)
  {
    if (!sdio1::set_block_size (FN, BLOCK) || !sdio1::enable_function (FN))
      {
        return fail ("function 1 enable");
      }
    if (!sdio1::write_byte (0, 0xF2, 0x7F))
      {
        return fail ("function 0 register 0xF2");
      }
    if (!sdio1::write_byte (FN, REG_BYTEMODE_ENABLE, 0x01))
      {
        return fail ("byte mode disable");
      }
    if (!sdio1::write_byte (0, 0x04, 0x07)
        || !sdio1::write_byte (FN, REG_INTR_ENABLE, 0x07))
      {
        return fail ("interrupt enable");
      }
    bus_up = true;
    return true;
  }

  // ------------------------------------------------------------------ RX

  void
  handle_message (const u8* msg, u32 length)
  {
    if (length < E2A_HEADER)
      {
        return;
      }
    const u16 id = get16 (msg);
    u16 param_len = get16 (msg + 6);
    if (E2A_HEADER + param_len > length)
      {
        param_len = static_cast<u16> (length - E2A_HEADER);
      }
    const u8* param = msg + E2A_HEADER;
    ++cnt.rx_messages;
    if (pending.waiting && !pending.done && id == pending.id)
      {
        if (pending.out != nullptr && pending.max != 0)
          {
            std::memset (pending.out, 0, pending.max);
            std::memcpy (pending.out, param,
                         param_len < pending.max ? param_len : pending.max);
          }
        pending.done = true;
        return;
      }
    if (on_message != nullptr)
      {
        on_message (id, param, param_len);
      }
  }

  // aicwf_process_rxframes(): one SDIO read holds several frames, each with
  // a 16-bit length first; a zero length ends the list.
  unsigned
  parse (const u8* buf, u32 n)
  {
    unsigned frames = 0;
    u32 p = 0;
    while (p + 4 <= n)
      {
        const u32 pkt_len = get16 (buf + p);
        if (pkt_len == 0)
          {
            break;
          }
        const u8 type = buf[p + 2];
        if ((type & TYPE_CFG) != TYPE_CFG)
          {
            // Data: the length is the MPDU's; the RX header is in front.
            const u32 total = pkt_len + aic8800::rx_header;
            if (p + total > n)
              {
                break;
              }
            ++cnt.rx_data;
            ++frames;
            if (on_data != nullptr)
              {
                on_data (buf + p, total);
              }
            p += (total + 3u) & ~3u;
          }
        else
          {
            if (p + 4 + pkt_len > n)
              {
                break;
              }
            ++frames;
            const u8 kind = type & 0x7F;
            if (kind == TYPE_CFG_CMD_RSP)
              {
                handle_message (buf + p + 4, pkt_len);
              }
            else if (kind == TYPE_CFG_PRINT)
              {
                // rwnx_rx_handle_print(): a firmware log line.
                if (cnt.rx_prints++ < 16)
                  {
                    kprintf ("[aic] fw: %.*s\n",
                             static_cast<int> (pkt_len > 120 ? 120 : pkt_len),
                             reinterpret_cast<const char*> (buf + p + 4));
                  }
              }
            p += ((pkt_len + 3u) & ~3u) + 4u;
          }
      }
    return frames;
  }

  // ------------------------------------------------------------------ TX

  // aicwf_sdio_tx_msg(): word alignment, the 4-byte "link tail", whole
  // blocks, flow control, then the write FIFO.
  bool
  send_message_frame (u32 payload_len)
  {
    payload_len = (payload_len + 3u) & ~3u;
    u32 len = payload_len;
    if ((len % BLOCK) != 0)
      {
        std::memset (msg_buf + payload_len, 0, TAIL_LEN);
        payload_len += TAIL_LEN;
        len = (payload_len / BLOCK + 1u) * BLOCK;
      }
    if (len > sizeof (msg_buf))
      {
        return fail ("message too long");
      }
    unsigned buffers = 0;
    for (unsigned retry = 0; retry < 10u; ++retry)
      {
        buffers = flow_ctrl ();
        if (buffers > 0 && len < buffers * BUFFER_SIZE)
          {
            break;
          }
        buffers = 0;
      }
    if (buffers == 0)
      {
        ++cnt.tx_busy;
        return fail ("message: no firmware buffer (flow control)");
      }
    if (!sdio1::write_fifo (FN, REG_WR_FIFO, msg_buf, len))
      {
        ++cnt.io_errors;
        return fail ("message write");
      }
    ++cnt.tx_messages;
    return true;
  }

  // ------------------------------------------------------------ boot ROM

  bool
  mem_read (u32 addr, u32& value)
  {
    u8 req[4];
    u8 cfm[8] = {};
    put32 (req, addr);
    if (!aic8800::request (DBG_MEM_READ_REQ, aic8800::TASK_DBG, req,
                           sizeof (req), DBG_MEM_READ_CFM, cfm, sizeof (cfm)))
      {
        return false;
      }
    value = get32 (cfm + 4);
    return true;
  }

  bool
  mem_write (u32 addr, u32 value)
  {
    u8 req[8];
    put32 (req, addr);
    put32 (req + 4, value);
    return aic8800::request (DBG_MEM_WRITE_REQ, aic8800::TASK_DBG, req,
                             sizeof (req), DBG_MEM_WRITE_CFM);
  }

  // rwnx_plat_bin_fw_upload_android(): 1 KiB at a time.
  bool
  upload (u32 addr, const u8* image, u32 size)
  {
    static u8 req[8 + 1024];
    for (u32 at = 0; at < size; at += 1024)
      {
        const u32 chunk = (size - at < 1024u) ? size - at : 1024u;
        std::memset (req, 0, sizeof (req));
        put32 (req, addr + at);
        put32 (req + 4, chunk);
        std::memcpy (req + 8, image + at, chunk);
        if (!aic8800::request (DBG_MEM_BLOCK_WRITE_REQ, aic8800::TASK_DBG, req,
                               sizeof (req), DBG_MEM_BLOCK_WRITE_CFM))
          {
            return false;
          }
      }
    return true;
  }

  // aicwifi_patch_config_8800d80(): the image publishes, at +0x198, where
  // its configuration lives; the host appends (offset, value) pairs to a
  // table the firmware applies when it starts.
  //
  //   0x00b4  0xf3010001   5 GHz enabled (USE_5G)
  //   0x0188  0x00000001   user_ext_flags: USER_PWROFST_COVER_CALIB_FLAG
  bool
  patch_config (aic8800::chip& out)
  {
    constexpr u32 rd_patch_addr = RAM_FMAC_FW_ADDR + 0x0198u;
    constexpr u32 pairs[][2] = {
      { 0x00b4u, 0xf3010001u },
      { 0x0188u, 0x00000001u },
    };
    constexpr u32 pair_count = sizeof (pairs) / sizeof (pairs[0]);
    // aic_patch_t: magic_num, pair_start, magic_num_2, pair_count,
    // block_dst[4], block_src[4], block_size[4].
    constexpr u32 OFS_MAGIC = 0, OFS_PAIR_START = 4, OFS_MAGIC_2 = 8,
                  OFS_PAIR_COUNT = 12, OFS_BLOCK_SIZE = 48;

    u32 config_base = 0, patch_str_base = 0, start_addr = 0x0016F800u;
    if (!mem_read (rd_patch_addr, config_base)
        || !mem_read (rd_patch_addr + 8, patch_str_base)
        || !mem_read (RAM_FMAC_FW_ADDR + 0x01Cu, out.fw_rom_version))
      {
        return false;
      }
    if (out.fw_rom_version > 0x06090100u)
      {
        // NEW_PATCH_BUFFER_MAP: the image names its own pair buffer.
        if (!mem_read (rd_patch_addr + 12, start_addr))
          {
            return false;
          }
      }
    if (!mem_write (patch_str_base + OFS_MAGIC, 0x48435450u)       // "PTCH"
        || !mem_write (patch_str_base + OFS_MAGIC_2, 0x50544348u)  // "HCTP"
        || !mem_write (patch_str_base + OFS_PAIR_START, start_addr)
        || !mem_write (patch_str_base + OFS_PAIR_COUNT, pair_count))
      {
        return false;
      }
    for (u32 i = 0; i < pair_count; ++i)
      {
        if (!mem_write (start_addr + 8 * i, pairs[i][0] + config_base)
            || !mem_write (start_addr + 8 * i + 4, pairs[i][1]))
          {
            return false;
          }
      }
    for (u32 i = 0; i < 4; ++i)
      {
        if (!mem_write (patch_str_base + OFS_BLOCK_SIZE + 4 * i, 0))
          {
            return false;
          }
      }
    return true;
  }
} // namespace

namespace aic8800
{
  const char*
  last_error (void)
  {
    return err;
  }

  const counters&
  stats (void)
  {
    return cnt;
  }

  bool
  running (void)
  {
    return fw_running;
  }

  void
  set_message_handler (message_handler handler)
  {
    on_message = handler;
  }

  void
  set_data_handler (data_handler handler)
  {
    on_data = handler;
  }

  // aicwf_sdio_hal_irqhandler(), D80 branch, called instead of taken.
  unsigned
  poll (void)
  {
    if (!bus_up || in_poll)
      {
        return 0;
      }
    in_poll = true;
    unsigned frames = 0;
    for (unsigned round = 0; round < 8u; ++round)
      {
        u8 status = 0;
        if (!sdio1::read_byte (FN, REG_MISC_INT_STATUS, status))
          {
            ++cnt.io_errors;
            break;
          }
        if (status & SDIO_OTHER_INTERRUPT)
          {
            // The device-to-host soft interrupt: acknowledge it.
            u8 pend = 0;
            if (sdio1::read_byte (FN, REG_INTR_PENDING, pend))
              {
                (void)sdio1::write_byte (FN, REG_INTR_PENDING,
                                         static_cast<u8> (pend & ~0x01u));
              }
            status &= static_cast<u8> (~SDIO_OTHER_INTERRUPT);
          }
        if (status == 0)
          {
            break;
          }
        if ((status | 0x08u) > 120u)
          {
            // 120 = function 1 in byte mode (disabled above); 121..127 =
            // function 2, the Bluetooth/message function, which is not
            // enabled here.  Neither is expected; count it and stop.
            if (cnt.rx_other++ == 0)
              {
                kprintf ("[aic] unexpected RX status 0x%02x (function 2 or "
                         "byte mode)\n",
                         static_cast<unsigned> (status));
              }
            break;
          }
        const u32 bytes = (status & 0x7Fu) * BLOCK;
        if (!sdio1::read_fifo (FN, REG_RD_FIFO, rx_buf, bytes))
          {
            ++cnt.io_errors;
            break;
          }
        ++cnt.rx_reads;
        frames += parse (rx_buf, bytes);
      }
    in_poll = false;
    return frames;
  }

  // rwnx_set_cmd_tx(): [len+4 | type 0x11 | crc8] [dummy word] [lmac_msg]
  // where len = sizeof (lmac_msg) + param_len.
  bool
  request (std::uint16_t id, std::uint16_t dest_task, const void* param,
           std::uint16_t param_len, std::uint16_t cfm_id, void* cfm,
           std::uint16_t cfm_max, std::uint32_t timeout_ms)
  {
    if (!bus_up)
      {
        std::snprintf (err, sizeof (err), "request 0x%04x: bus down", id);
        return false;
      }
    if (in_poll || in_request)
      {
        // A handler asked for a request: the receive buffer is in use.
        std::snprintf (err, sizeof (err),
                       "request 0x%04x from inside a handler", id);
        return false;
      }
    const u32 len = 8u + param_len;
    if (len + 8u + TAIL_LEN > sizeof (msg_buf))
      {
        std::snprintf (err, sizeof (err), "request 0x%04x: %u bytes", id,
                       static_cast<unsigned> (param_len));
        return false;
      }
    in_request = true;
    std::memset (msg_buf, 0, sizeof (msg_buf));
    msg_buf[0] = static_cast<u8> ((len + 4u) & 0xFFu);
    msg_buf[1] = static_cast<u8> (((len + 4u) >> 8) & 0x0Fu);
    msg_buf[2] = TYPE_CFG_CMD_RSP;
    msg_buf[3] = crc8 (msg_buf, 3);
    // msg_buf[4..7]: the dummy word.
    put16 (msg_buf + 8, id);
    put16 (msg_buf + 10, dest_task);
    put16 (msg_buf + 12, DRV_TASK_ID);
    put16 (msg_buf + 14, param_len);
    if (param_len != 0)
      {
        std::memcpy (msg_buf + 16, param, param_len);
      }

    pending.waiting = true;
    pending.done = false;
    pending.id = cfm_id;
    pending.out = cfm;
    pending.max = cfm_max;

    bool ok = send_message_frame (len + 8u);
    if (ok)
      {
        const std::uint32_t start = wifi_platform::millis ();
        unsigned spins = 0;
        for (;;)
          {
            (void)poll ();
            if (pending.done)
              {
                break;
              }
            if (wifi_platform::millis () - start >= timeout_ms)
              {
                ++cnt.timeouts;
                std::snprintf (err, sizeof (err),
                               "request 0x%04x: no confirmation 0x%04x in %u "
                               "ms",
                               id, cfm_id, static_cast<unsigned> (timeout_ms));
                ok = false;
                break;
              }
            // A confirmation is usually there within a few hundred µs.
            if (++spins < 40u)
              {
                sdio1::delay_us (100);
              }
            else
              {
                wifi_platform::sleep_ms (1);
              }
          }
      }
    pending.waiting = false;
    in_request = false;
    return ok;
  }

  // aicwf_sdio_aggr() for one packet + aicwf_sdio_aggr_send():
  // [len | type 0x01 | crc8] [hostdesc, 28 bytes] [payload], word aligned,
  // the link tail, whole blocks.
  bool
  send_data (const std::uint8_t* dst, const std::uint8_t* src,
             const std::uint8_t* ethertype, const std::uint8_t* payload,
             std::uint32_t length, std::uint8_t vif, std::uint8_t sta,
             std::uint8_t tid)
  {
    constexpr u32 HOSTDESC = 28;
    if (!fw_running || length > 1600u)
      {
        return false;
      }
    if (flow_ctrl () <= DATA_FLOW_CTRL_THRESH)
      {
        ++cnt.tx_busy;
        return false;
      }
    u8* b = data_buf;
    const u32 body = HOSTDESC + length;
    b[0] = static_cast<u8> (body & 0xFFu);
    b[1] = static_cast<u8> ((body >> 8) & 0x0Fu);
    b[2] = TYPE_DATA_TX;
    b[3] = crc8 (b, 3);
    // struct hostdesc (ipc_shared.h, CONFIG_RWNX_FULLMAC, no MU-MIMO).
    u8* d = b + 4;
    std::memset (d, 0, HOSTDESC);
    put16 (d + 0, static_cast<u16> (length)); // packet_len
    // flags_ext @2 = 0; hostid @4 = 0: no transmit confirmation wanted
    std::memcpy (d + 8, dst, 6);              // eth_dest_addr
    std::memcpy (d + 14, src, 6);             // eth_src_addr
    d[20] = ethertype[0];                     // ethertype, as in the frame
    d[21] = ethertype[1];
    d[22] = 1;                                // ac: RWNX_HWQ_BE
    d[23] = tid;
    d[24] = vif;
    d[25] = sta;
    // flags @26 = 0
    std::memcpy (d + HOSTDESC, payload, length);

    u32 total = (4u + body + 3u) & ~3u;
    std::memset (b + 4u + body, 0, total - (4u + body));
    if ((total % BLOCK) != 0)
      {
        std::memset (b + total, 0, TAIL_LEN);
        total += TAIL_LEN;
      }
    const u32 padded = (total + BLOCK - 1u) / BLOCK * BLOCK;
    std::memset (b + total, 0, padded - total);
    if (!sdio1::write_fifo (FN, REG_WR_FIFO, b, padded))
      {
        ++cnt.io_errors;
        return false;
      }
    ++cnt.tx_data;
    return true;
  }

  // aicbsp_sdio_probe() -> aicbsp_driver_fw_init() -> aicwifi_init(), the
  // AIC8800D80 path, without the Bluetooth patches (aicbt_init): the radio
  // is used for Wi-Fi only here.
  bool
  boot (chip& out)
  {
    out = {};
    fw_running = false;
    bus_up = false;
    err[0] = '\0';

    if (!sdio1::init (out.sdio))
      {
        return fail ("SDIO1 bring-up");
      }
    // Linux matches on the function's own CISTPL_MANFID when it has one.
    out.vendor = out.sdio.f1_vendor != 0 ? out.sdio.f1_vendor : out.sdio.vendor;
    out.device = out.sdio.f1_vendor != 0 ? out.sdio.f1_device : out.sdio.device;
    if (out.vendor != VENDOR_AIC8800 || out.device != DEVICE_AIC8800D80)
      {
        const char* what = "not an AIC8800";
        if (out.vendor == VENDOR_AIC8800 && out.device == DEVICE_AIC8800DC)
          {
            what = "AIC8800DC/DW (needs the 8800DC firmware set)";
          }
        else if (out.vendor == VENDOR_AIC8800
                 && out.device == DEVICE_AIC8800D80X2)
          {
            what = "AIC8800D80X2 (needs the D80X2 firmware set)";
          }
        else if (out.vendor == VENDOR_AIC8800 && (out.device & 0xFFF0u) == 0x9080u)
          {
            what = "AIC8800D80N/D40N family (needs the D80N firmware set)";
          }
        else if (out.vendor == VENDOR_AIC8801)
          {
            what = "AIC8801 (older register map)";
          }
        std::snprintf (err, sizeof (err),
                       "SDIO id %04x:%04x is not the AIC8800D80/D40 this "
                       "driver supports: %s",
                       out.vendor, out.device, what);
        return false;
      }

    if (!function_init ())
      {
        return false;
      }

    // aicbsp_driver_fw_init(): which silicon, hence which image.
    if (!mem_read (CHIP_ID_ADDR, out.chip_id_word))
      {
        return false;
      }
    out.revision = static_cast<u8> ((out.chip_id_word >> 16) & 0x3Fu);
    out.id_h = ((out.chip_id_word >> 16) & 0xC0u) == 0xC0u;
    const u8* image = nullptr;
    if (out.id_h)
      {
        image = aic8800_fw_h_u02_start;
        out.fw_size
            = static_cast<u32> (aic8800_fw_h_u02_end - aic8800_fw_h_u02_start);
      }
    else if (out.revision == CHIP_REV_U02 || out.revision == CHIP_REV_U03)
      {
        image = aic8800_fw_u02_start;
        out.fw_size
            = static_cast<u32> (aic8800_fw_u02_end - aic8800_fw_u02_start);
      }
    else
      {
        std::snprintf (err, sizeof (err),
                       "chip revision %u%s (id word 0x%08x): no firmware for "
                       "it here",
                       static_cast<unsigned> (out.revision),
                       out.revision == CHIP_REV_U01 ? " (U01)" : "",
                       static_cast<unsigned> (out.chip_id_word));
        return false;
      }

    if (!upload (RAM_FMAC_FW_ADDR, image, out.fw_size) || !patch_config (out))
      {
        return false;
      }

    // aicwifi_start_from_bootrom().
    {
      u8 req[8];
      u8 cfm[4] = {};
      put32 (req, RAM_FMAC_FW_ADDR);
      put32 (req + 4, HOST_START_APP_AUTO);
      if (!request (DBG_START_APP_REQ, TASK_DBG, req, sizeof (req),
                    DBG_START_APP_CFM, cfm, sizeof (cfm)))
        {
          return false;
        }
      out.boot_status = get32 (cfm);
    }

    // The firmware takes over the SDIO function: Linux probes it again
    // (aicwf_sdio_probe -> aicwf_sdiov3_func_init -> aicwf_sdio_bus_start).
    wifi_platform::sleep_ms (50);
    if (!function_init ())
      {
        return false;
      }
    fw_running = true;
    return true;
  }
} // namespace aic8800
