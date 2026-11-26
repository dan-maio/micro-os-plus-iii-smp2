/*
 * file-server.cpp -- TCP file server for the RAM disk (protocol in
 * file-server.hpp), lwIP raw API on the radio thread.
 *
 * The RAM disk is memory, so a "put" writes each TCP segment to the file
 * inside the receive callback and reopens the window at once.  A "get"
 * keeps the file open and queues the next piece (up to 4 KiB) whenever the
 * TCP send buffer has room -- the "sent" callback, as the PC acknowledges --
 * so the file streams at the rate the link carries.
 *
 * After a "put" the file is closed, opened again and read back through
 * FatFs; only when its size and CRC-32 match what arrived is "OK <crc32>"
 * sent.  The CRC the PC compares therefore describes what is stored.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "file-server.hpp"
#include "ram-disk.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string_view>

extern "C"
{
#include <chan-fatfs/ff.h>

#include "lwip/dhcp.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"

  // Both network stacks of this board provide it (net-lwip.cpp,
  // wifi-lwip.cpp): printf to the console, one writer at a time.
  void kprintf (const char* format, ...)
      __attribute__ ((format (printf, 1, 2)));
}

namespace file_server
{
  void
  list_files (const char* title);

  namespace
  {
    // ------------------------------------------------------------ CRC-32
    // IEEE 802.3 / zlib (Python's zlib.crc32), table built at compile time.
    constexpr std::array<std::uint32_t, 256> crc_table = [] {
      std::array<std::uint32_t, 256> t{};
      for (std::uint32_t i = 0; i < 256; ++i)
        {
          std::uint32_t c = i;
          for (int bit = 0; bit < 8; ++bit)
            {
              c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
            }
          t[i] = c;
        }
      return t;
    }();

    constexpr std::uint32_t
    crc_update (std::uint32_t crc, const std::uint8_t* data,
                std::size_t length)
    {
      for (std::size_t i = 0; i < length; ++i)
        {
          crc = crc_table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
        }
      return crc;
    }

    constexpr std::uint8_t crc_check[9]
        = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    static_assert ((crc_update (0xffffffffu, crc_check, 9) ^ 0xffffffffu)
                   == 0xcbf43926u);

    // ----------------------------------------------------------- state
    FATFS volume;
    bool mounted = false;
    alignas (8) std::uint8_t mkfs_work[32 * 1024];

    enum class mode : std::uint8_t
    {
      command,
      receiving,
      sending
    };

    struct session
    {
      tcp_pcb* pcb{ nullptr };
      mode state{ mode::command };
      char line[200]{};
      std::size_t line_length{ 0 };
      bool line_overflow{ false };
      char path[112]{}; // "/<name>"
      std::uint64_t remaining{ 0 };
      std::uint64_t size{ 0 };
      std::uint32_t crc{ 0 };
      std::uint32_t started_ms{ 0 };
      bool write_failed{ false };
    };

    session client;
    FIL incoming;
    FIL outgoing;

    // Reply text waiting for room in the send buffer ("ls" can be long).
    char reply[32 * 1024];
    std::size_t reply_length = 0;
    std::size_t reply_sent = 0;

    alignas (8) std::uint8_t block[4096];

    counters cnt;

    bool got_verdict = false;
    bool verdict_ok = false;
    char verdict_msg[120] = "";

    udp_pcb* beacon = nullptr;
    const char* beacon_tag = "VF2-RAM-DISK";

    void
    beacon_tick (void*);

    // A static address requested from another thread, applied in the timer.
    volatile bool static_pending = false;
    std::uint32_t static_ip = 0, static_mask = 0, static_gw = 0;

    // ---------------------------------------------------------- output
    void
    say (std::string_view text)
    {
      const std::size_t room = sizeof (reply) - reply_length;
      const std::size_t n = text.size () < room ? text.size () : room;
      std::memcpy (reply + reply_length, text.data (), n);
      reply_length += n;
    }

    void
    say_number (std::uint64_t value)
    {
      char text[24];
      std::snprintf (text, sizeof (text), "%llu",
                     static_cast<unsigned long long> (value));
      say (text);
    }

    void
    say_hex32 (std::uint32_t value)
    {
      char text[12];
      std::snprintf (text, sizeof (text), "%08x",
                     static_cast<unsigned> (value));
      say (text);
    }

    void
    say_error (const char* what)
    {
      say ("ERR ");
      say (what);
      say ("\n");
      kprintf ("[file] error: %s\n", what);
    }

    const char*
    describe (FRESULT r)
    {
      switch (r)
        {
        case FR_OK:
          return "ok";
        case FR_DISK_ERR:
          return "disk error";
        case FR_NOT_READY:
          return "RAM disk not ready";
        case FR_NO_FILE:
          return "no such file";
        case FR_DENIED:
          return "denied (disk full?)";
        case FR_INVALID_NAME:
          return "invalid name";
        case FR_NO_FILESYSTEM:
          return "no filesystem";
        case FR_MKFS_ABORTED:
          return "mkfs aborted";
        default:
          return "filesystem error";
        }
    }

    bool
    make_filesystem (void)
    {
      // FAT32, 4 KiB clusters, no partition table: 131072 clusters.
      const FRESULT r = f_mkfs ("", FM_FAT32 | FM_SFD, 4096, mkfs_work,
                                sizeof (mkfs_work));
      if (r != FR_OK)
        {
          kprintf ("[disk] f_mkfs: %s (%d)\n", describe (r),
                   static_cast<int> (r));
          return false;
        }
      return true;
    }

    void
    finish_sending (void)
    {
      f_close (&outgoing);
      kprintf ("[file] sent %s (%llu bytes) in %u ms\n", client.path + 1,
               static_cast<unsigned long long> (client.size),
               static_cast<unsigned> (sys_now ()
                                      - client.started_ms));
      ++cnt.files_sent;
      client.state = mode::command;
      say ("OK\n");
    }

    // Pending reply text, then (in "get") file data, into the send buffer.
    void
    pump_output (void)
    {
      tcp_pcb* pcb = client.pcb;
      if (pcb == nullptr)
        {
          return;
        }
      for (;;)
        {
          const std::size_t room = tcp_sndbuf (pcb);
          if (room == 0)
            {
              break;
            }
          if (reply_sent < reply_length)
            {
              const std::size_t left = reply_length - reply_sent;
              const std::size_t n = left < room ? left : room;
              if (tcp_write (pcb, reply + reply_sent,
                             static_cast<u16_t> (n), TCP_WRITE_FLAG_COPY)
                  != ERR_OK)
                {
                  break;
                }
              reply_sent += n;
              if (reply_sent == reply_length)
                {
                  reply_sent = 0;
                  reply_length = 0;
                }
              continue;
            }
          if (client.state != mode::sending)
            {
              break;
            }
          if (client.remaining == 0)
            {
              finish_sending ();
              continue;
            }
          std::size_t want = sizeof (block);
          if (want > room)
            {
              want = room;
            }
          if (want > client.remaining)
            {
              want = static_cast<std::size_t> (client.remaining);
            }
          UINT got = 0;
          if (f_read (&outgoing, block, static_cast<UINT> (want), &got)
                  != FR_OK
              || got == 0)
            {
              // The size is already announced: drop the connection so the
              // PC sees a short transfer rather than padded data.
              kprintf ("[file] read error while sending, closing\n");
              f_close (&outgoing);
              client.state = mode::command;
              tcp_abort (pcb);
              client.pcb = nullptr;
              return;
            }
          if (tcp_write (pcb, block, static_cast<u16_t> (got),
                         TCP_WRITE_FLAG_COPY)
              != ERR_OK)
            {
              // No segment memory now: step back, retry on the next "sent".
              f_lseek (&outgoing, f_tell (&outgoing) - got);
              break;
            }
          client.remaining -= got;
          cnt.bytes_sent += got;
        }
      tcp_output (pcb);
    }

    // ----------------------------------------------------------- commands
    std::string_view
    next_token (std::string_view& rest)
    {
      while (!rest.empty () && rest.front () == ' ')
        {
          rest.remove_prefix (1);
        }
      std::size_t n = 0;
      while (n < rest.size () && rest[n] != ' ')
        {
          ++n;
        }
      const std::string_view token{ rest.data (), n };
      rest.remove_prefix (n);
      return token;
    }

    // "/<name>": plain names in the root directory only.
    bool
    make_path (std::string_view name)
    {
      if (name.empty () || name.size () + 2 > sizeof (client.path))
        {
          return false;
        }
      for (const char c : name)
        {
          if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?'
              || c == '"' || c == '<' || c == '>' || c == '|'
              || static_cast<unsigned char> (c) < 0x20)
            {
              return false;
            }
        }
      client.path[0] = '/';
      std::memcpy (client.path + 1, name.data (), name.size ());
      client.path[1 + name.size ()] = '\0';
      return true;
    }

    // CRC-32 and size of a file as stored on the disk.
    bool
    file_crc (const char* path, std::uint32_t& crc, std::uint64_t& size)
    {
      FIL f;
      if (f_open (&f, path, FA_READ) != FR_OK)
        {
          return false;
        }
      size = f_size (&f);
      std::uint32_t value = 0xffffffffu;
      for (;;)
        {
          UINT got = 0;
          if (f_read (&f, block, sizeof (block), &got) != FR_OK)
            {
              f_close (&f);
              return false;
            }
          if (got == 0)
            {
              break;
            }
          value = crc_update (value, block, got);
        }
      crc = value ^ 0xffffffffu;
      return f_close (&f) == FR_OK;
    }

    bool
    disk_info (std::uint64_t& total, std::uint64_t& free_bytes)
    {
      DWORD free_clusters = 0;
      FATFS* fs = nullptr;
      if (f_getfree ("", &free_clusters, &fs) != FR_OK || fs == nullptr)
        {
          return false;
        }
      const std::uint64_t cluster
          = static_cast<std::uint64_t> (fs->csize) * ram_disk::sector_size;
      free_bytes = static_cast<std::uint64_t> (free_clusters) * cluster;
      total = static_cast<std::uint64_t> (fs->n_fatent - 2) * cluster;
      return true;
    }

    void
    finish_receiving (void)
    {
      const bool closed = f_close (&incoming) == FR_OK;
      const std::uint32_t received_crc = client.crc ^ 0xffffffffu;
      client.state = mode::command;
      if (client.write_failed || !closed)
        {
          f_unlink (client.path);
          say_error ("write failed (RAM disk full?)");
          return;
        }
      std::uint32_t stored_crc = 0;
      std::uint64_t stored_size = 0;
      if (!file_crc (client.path, stored_crc, stored_size)
          || stored_size != client.size || stored_crc != received_crc)
        {
          f_unlink (client.path);
          say_error ("verification of the stored file failed");
          return;
        }
      kprintf ("[file] stored %s (%llu bytes, crc32 %08x) in %u ms\n",
               client.path + 1,
               static_cast<unsigned long long> (client.size),
               static_cast<unsigned> (stored_crc),
               static_cast<unsigned> (sys_now ()
                                      - client.started_ms));
      ++cnt.files_received;
      say ("OK ");
      say_hex32 (stored_crc);
      say ("\n");
    }

    void
    list_entries (bool to_client)
    {
      DIR dir;
      FILINFO fi;
      if (f_opendir (&dir, "/") != FR_OK)
        {
          return;
        }
      while (f_readdir (&dir, &fi) == FR_OK && fi.fname[0] != '\0')
        {
          if (fi.fattrib & AM_DIR)
            {
              continue;
            }
          if (to_client)
            {
              say_number (fi.fsize);
              say (" ");
              say (fi.fname);
              say ("\n");
            }
          else
            {
              kprintf ("[disk]   %12llu %s\n",
                       static_cast<unsigned long long> (fi.fsize), fi.fname);
            }
        }
      f_closedir (&dir);
    }

    void
    execute (std::string_view line)
    {
      std::string_view rest = line;
      const auto verb = next_token (rest);
      if (verb.empty ())
        {
          return;
        }
      if (!mounted && verb != "verdict")
        {
          say_error ("RAM disk not mounted");
          return;
        }

      if (verb == "ls")
        {
          list_entries (true);
          say ("OK\n");
        }
      else if (verb == "df")
        {
          std::uint64_t total = 0, free_bytes = 0;
          if (!disk_info (total, free_bytes))
            {
              say_error ("getfree failed");
              return;
            }
          say ("TOTAL ");
          say_number (total);
          say (" FREE ");
          say_number (free_bytes);
          say ("\nOK\n");
        }
      else if (verb == "put")
        {
          const auto name = next_token (rest);
          const auto size_text = next_token (rest);
          std::uint64_t size = 0;
          bool valid = !size_text.empty ();
          for (const char c : size_text)
            {
              if (c < '0' || c > '9')
                {
                  valid = false;
                  break;
                }
              size = size * 10u + static_cast<std::uint64_t> (c - '0');
            }
          if (!valid || !make_path (name))
            {
              say_error ("usage: put <name> <size>");
              return;
            }
          std::uint64_t total = 0, free_bytes = 0, old_size = 0;
          FILINFO fi;
          if (f_stat (client.path, &fi) == FR_OK)
            {
              old_size = fi.fsize;
            }
          if (disk_info (total, free_bytes) && size > free_bytes + old_size)
            {
              say_error ("not enough free space on the RAM disk");
              return;
            }
          const FRESULT r
              = f_open (&incoming, client.path, FA_CREATE_ALWAYS | FA_WRITE);
          if (r != FR_OK)
            {
              say_error (describe (r));
              return;
            }
          client.size = size;
          client.remaining = size;
          client.crc = 0xffffffffu;
          client.write_failed = false;
          client.started_ms = sys_now ();
          client.state = mode::receiving;
          say ("READY\n");
          if (size == 0)
            {
              finish_receiving ();
            }
        }
      else if (verb == "get")
        {
          const auto name = next_token (rest);
          if (!make_path (name))
            {
              say_error ("usage: get <name>");
              return;
            }
          std::uint32_t crc = 0;
          std::uint64_t size = 0;
          if (!file_crc (client.path, crc, size)
              || f_open (&outgoing, client.path, FA_READ) != FR_OK)
            {
              say_error ("no such file");
              return;
            }
          client.size = size;
          client.remaining = size;
          client.started_ms = sys_now ();
          client.state = mode::sending;
          say ("DATA ");
          say_number (size);
          say (" ");
          say_hex32 (crc);
          say ("\n");
        }
      else if (verb == "rm")
        {
          const auto name = next_token (rest);
          if (!make_path (name) || f_unlink (client.path) != FR_OK)
            {
              say_error ("no such file");
              return;
            }
          kprintf ("[file] removed %s\n", client.path + 1);
          say ("OK\n");
        }
      else if (verb == "format")
        {
          f_mount (nullptr, "", 0);
          mounted = false;
          if (!make_filesystem () || f_mount (&volume, "", 1) != FR_OK)
            {
              say_error ("format failed");
              return;
            }
          mounted = true;
          kprintf ("[disk] formatted\n");
          say ("OK\n");
        }
      else if (verb == "verdict")
        {
          const auto word = next_token (rest);
          while (!rest.empty () && rest.front () == ' ')
            {
              rest.remove_prefix (1);
            }
          const std::size_t n = rest.size () < sizeof (verdict_msg) - 1
                                    ? rest.size ()
                                    : sizeof (verdict_msg) - 1;
          std::memcpy (verdict_msg, rest.data (), n);
          verdict_msg[n] = '\0';
          verdict_ok = word == "PASS";
          got_verdict = true;
          kprintf ("[file] verdict from the PC: %s %s\n",
                   verdict_ok ? "PASS" : "FAIL", verdict_msg);
          // The final listing here, in the lwIP callback, where FatFs is
          // already serialised with every other file operation.
          list_files ("at the end");
          say ("OK\n");
        }
      else
        {
          say_error ("unknown command (ls, df, put, get, rm, format, verdict)");
        }
    }

    void
    consume (const std::uint8_t* data, std::size_t length)
    {
      while (length != 0)
        {
          if (client.state == mode::receiving)
            {
              const std::size_t n
                  = length < client.remaining
                        ? length
                        : static_cast<std::size_t> (client.remaining);
              if (!client.write_failed)
                {
                  UINT written = 0;
                  if (f_write (&incoming, data, static_cast<UINT> (n),
                               &written)
                          != FR_OK
                      || written != n)
                    {
                      client.write_failed = true; // keep draining
                    }
                }
              client.crc = crc_update (client.crc, data, n);
              client.remaining -= n;
              cnt.bytes_received += n;
              data += n;
              length -= n;
              if (client.remaining == 0)
                {
                  finish_receiving ();
                }
              continue;
            }
          const char c = static_cast<char> (*data++);
          --length;
          if (c == '\n')
            {
              if (client.line_overflow)
                {
                  say_error ("line too long");
                }
              else if (client.state == mode::sending)
                {
                  say_error ("busy sending");
                }
              else
                {
                  std::size_t end = client.line_length;
                  if (end != 0 && client.line[end - 1] == '\r')
                    {
                      --end;
                    }
                  execute ({ client.line, end });
                }
              client.line_length = 0;
              client.line_overflow = false;
            }
          else if (client.line_length < sizeof (client.line))
            {
              client.line[client.line_length++] = c;
            }
          else
            {
              client.line_overflow = true;
            }
        }
    }

    // ------------------------------------------------------ lwIP callbacks
    void
    drop_client (const char* why)
    {
      if (client.state == mode::receiving)
        {
          f_close (&incoming);
          f_unlink (client.path);
          kprintf ("[file] upload of %s abandoned\n", client.path + 1);
        }
      else if (client.state == mode::sending)
        {
          f_close (&outgoing);
        }
      kprintf ("[file] client %s\n", why);
      client = session{};
      reply_length = 0;
      reply_sent = 0;
    }

    err_t
    on_received (void*, tcp_pcb* pcb, pbuf* p, err_t)
    {
      if (p == nullptr)
        {
          tcp_arg (pcb, nullptr);
          tcp_recv (pcb, nullptr);
          tcp_sent (pcb, nullptr);
          tcp_err (pcb, nullptr);
          tcp_poll (pcb, nullptr, 0);
          drop_client ("disconnected");
          if (tcp_close (pcb) != ERR_OK)
            {
              tcp_abort (pcb);
              return ERR_ABRT;
            }
          return ERR_OK;
        }
      for (pbuf* q = p; q != nullptr; q = q->next)
        {
          consume (static_cast<const std::uint8_t*> (q->payload), q->len);
        }
      tcp_recved (pcb, p->tot_len);
      pbuf_free (p);
      if (client.pcb == nullptr)
        {
          return ERR_ABRT;
        }
      pump_output ();
      return client.pcb == nullptr ? ERR_ABRT : ERR_OK;
    }

    err_t
    on_sent (void*, tcp_pcb*, u16_t)
    {
      pump_output ();
      return client.pcb == nullptr ? ERR_ABRT : ERR_OK;
    }

    err_t
    on_poll (void*, tcp_pcb*)
    {
      pump_output ();
      return client.pcb == nullptr ? ERR_ABRT : ERR_OK;
    }

    void
    on_error (void*, err_t)
    {
      drop_client ("connection reset"); // lwIP has freed the pcb
    }

    err_t
    on_accepted (void*, tcp_pcb* pcb, err_t error)
    {
      if (error != ERR_OK || pcb == nullptr)
        {
          return ERR_VAL;
        }
      if (client.pcb != nullptr)
        {
          tcp_abort (pcb); // one client at a time
          return ERR_ABRT;
        }
      client = session{};
      client.pcb = pcb;
      reply_length = 0;
      reply_sent = 0;
      ++cnt.connections;
      tcp_arg (pcb, &client);
      tcp_recv (pcb, on_received);
      tcp_sent (pcb, on_sent);
      tcp_err (pcb, on_error);
      tcp_poll (pcb, on_poll, 1);
      tcp_nagle_disable (pcb);
      const ip4_addr_t* peer = ip_2_ip4 (&pcb->remote_ip);
      kprintf ("[file] client %u.%u.%u.%u:%u connected\n", ip4_addr1 (peer),
               ip4_addr2 (peer), ip4_addr3 (peer), ip4_addr4 (peer),
               pcb->remote_port);
      return ERR_OK;
    }
  } // namespace

  const counters&
  stats (void)
  {
    return cnt;
  }

  void
  request_static_address (std::uint32_t ip, std::uint32_t mask,
                          std::uint32_t gateway)
  {
    static_ip = ip;
    static_mask = mask;
    static_gw = gateway;
    static_pending = true;
  }

  bool
  verdict_received (void)
  {
    return got_verdict;
  }

  bool
  verdict_pass (void)
  {
    return verdict_ok;
  }

  const char*
  verdict_text (void)
  {
    return verdict_msg;
  }

  void
  list_files (const char* title)
  {
    if (!mounted)
      {
        return;
      }
    std::uint64_t total = 0, free_bytes = 0;
    (void)disk_info (total, free_bytes);
    kprintf ("[disk] ---- %s: RAM disk 0x%08lx..0x%08lx, FAT32, %llu KiB "
             "total, %llu KiB free ----\n",
             title, static_cast<unsigned long> (ram_disk::base),
             static_cast<unsigned long> (ram_disk::base + ram_disk::size_bytes
                                         - 1),
             static_cast<unsigned long long> (total / 1024),
             static_cast<unsigned long long> (free_bytes / 1024));
    list_entries (false);
  }

  bool
  mount_disk (void)
  {
    if (!ram_disk::probe ())
      {
        kprintf ("[disk] RAM disk unavailable: %s\n", ram_disk::last_error ());
        return false;
      }
    FRESULT r = f_mount (&volume, "", 1);
    if (r == FR_NO_FILESYSTEM)
      {
        kprintf ("[disk] no filesystem in the RAM: creating FAT32 ...\n");
        if (!make_filesystem ())
          {
            return false;
          }
        r = f_mount (&volume, "", 1);
      }
    if (r != FR_OK)
      {
        kprintf ("[disk] f_mount: %s (%d)\n", describe (r),
                 static_cast<int> (r));
        return false;
      }
    mounted = true;
    list_files ("mounted");
    return true;
  }

  bool
  start (const char* tag)
  {
    beacon_tag = tag;
    tcp_pcb* pcb = tcp_new_ip_type (IPADDR_TYPE_V4);
    if (pcb == nullptr || tcp_bind (pcb, IP_ANY_TYPE, tcp_port) != ERR_OK)
      {
        kprintf ("[file] cannot bind TCP port %u\n", tcp_port);
        return false;
      }
    tcp_pcb* listening = tcp_listen (pcb);
    if (listening == nullptr)
      {
        tcp_close (pcb);
        return false;
      }
    tcp_accept (listening, on_accepted);

    beacon = udp_new_ip_type (IPADDR_TYPE_V4);
    if (beacon != nullptr)
      {
        ip_set_option (beacon, SOF_BROADCAST);
      }
    sys_timeout (1000, beacon_tick, nullptr);
    kprintf ("[file] RAM-disk file server on TCP port %u, beacon \"%s\" on "
             "UDP %u\n",
             tcp_port, beacon_tag, beacon_port);
    return true;
  }

  namespace
  {
    // The beacon is an lwIP timer: it runs inside sys_check_timeouts(), on
    // whatever thread drives the stack's timers and under that stack's own
    // serialisation (the radio thread for Wi-Fi, the lwIP mutex for the
    // GMAC), so nothing here needs a lock of its own.
    void
    beacon_tick (void*)
    {
      sys_timeout (1000, beacon_tick, nullptr);
      netif* n = netif_default;
      if (static_pending && n != nullptr)
        {
          static_pending = false;
          ip4_addr_t a, m, g;
          IP4_ADDR (&a, static_ip >> 24, (static_ip >> 16) & 0xff,
                    (static_ip >> 8) & 0xff, static_ip & 0xff);
          IP4_ADDR (&m, static_mask >> 24, (static_mask >> 16) & 0xff,
                    (static_mask >> 8) & 0xff, static_mask & 0xff);
          IP4_ADDR (&g, static_gw >> 24, (static_gw >> 16) & 0xff,
                    (static_gw >> 8) & 0xff, static_gw & 0xff);
          dhcp_stop (n);
          netif_set_addr (n, &a, &m, &g);
          kprintf ("[file] static address %u.%u.%u.%u (DHCP stopped)\n",
                   ip4_addr1 (&a), ip4_addr2 (&a), ip4_addr3 (&a),
                   ip4_addr4 (&a));
        }
      if (beacon == nullptr || n == nullptr || !netif_is_link_up (n)
          || ip4_addr_isany_val (*netif_ip4_addr (n)))
        {
          return;
        }
      const ip4_addr_t* a = netif_ip4_addr (n);
      char text[64];
      const int len = std::snprintf (
          text, sizeof (text), "%s %u.%u.%u.%u %u\n", beacon_tag,
          ip4_addr1 (a), ip4_addr2 (a), ip4_addr3 (a), ip4_addr4 (a),
          tcp_port);
      pbuf* p
          = pbuf_alloc (PBUF_TRANSPORT, static_cast<u16_t> (len), PBUF_RAM);
      if (p == nullptr)
        {
          return;
        }
      std::memcpy (p->payload, text, static_cast<std::size_t> (len));
      if (udp_sendto (beacon, p, IP_ADDR_BROADCAST, beacon_port) == ERR_OK)
        {
          ++cnt.beacons;
        }
      pbuf_free (p);
    }
  } // namespace

} // namespace file_server
