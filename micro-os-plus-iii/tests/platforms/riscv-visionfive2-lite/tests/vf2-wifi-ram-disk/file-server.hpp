/*
 * file-server.hpp -- file transfer over Wi-Fi to the RAM disk: a TCP server
 * on port 5000 and a UDP "here I am" beacon on port 5001.
 *
 * The protocol is the MangoPi MQ-Pro's (host/wifi_fs.py works unchanged):
 * text lines ending in LF, raw bytes only where a size announced them.
 *
 *   ls                  -> "<size> <name>" per file, then "OK"
 *   df                  -> "TOTAL <bytes> FREE <bytes>", then "OK"
 *   put <name> <size>   -> "READY", <size> raw bytes, then "OK <crc32>"
 *                          (CRC-32 of the file as read back from the disk)
 *   get <name>          -> "DATA <size> <crc32>", <size> raw bytes, "OK"
 *   rm <name>           -> "OK"
 *   format              -> "OK"
 *   verdict PASS|FAIL [text] -> "OK"; the test then prints its RESULT
 *   failure             -> "ERR <reason>" (the connection stays usable)
 *
 * The beacon: once a second, a UDP broadcast to port 5001 with
 *   "<tag> <ip> 5000\n"      (tag: VF2-RAM-DISK for the Wi-Fi test,
 *                             VF2-NET-RAM-DISK for the GMAC test)
 * so the PC finds the board without reading its UART.
 *
 * Everything here -- the lwIP callbacks, the beacon (an lwIP timer) and
 * every FatFs call (FatFs is not reentrant) -- runs inside lwIP, so it is
 * serialised by whatever serialises the stack: the one radio thread of the
 * Wi-Fi test, the lwIP mutex of net-lwip.cpp.  Shared by vf2-wifi-ram-disk
 * and smp-net-ram-disk.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace file_server
{
  inline constexpr std::uint16_t tcp_port = 5000;
  inline constexpr std::uint16_t beacon_port = 5001;

  /// Mount (or create) the FAT filesystem on the RAM disk; print it.
  bool
  mount_disk (void);

  /// Start listening on tcp_port and the beacon timer.  After the stack's
  /// init(), before any other thread drives lwIP.
  bool
  start (const char* beacon_tag);

  /// Diagnostics: use a fixed address instead of waiting for DHCP.  Safe to
  /// call from any thread: the change is applied by the server's lwIP timer
  /// (within the next second), where the stack is serialised.
  void
  request_static_address (std::uint32_t ip, std::uint32_t mask,
                          std::uint32_t gateway); // host order, a.b.c.d = a<<24

  struct counters
  {
    std::uint32_t files_received{ 0 };
    std::uint32_t files_sent{ 0 };
    std::uint64_t bytes_received{ 0 };
    std::uint64_t bytes_sent{ 0 };
    std::uint32_t connections{ 0 };
    std::uint32_t beacons{ 0 };
  };

  [[nodiscard]] const counters&
  stats (void);

  /// The PC's verdict, once it sent one.
  [[nodiscard]] bool
  verdict_received (void);
  [[nodiscard]] bool
  verdict_pass (void);
  [[nodiscard]] const char*
  verdict_text (void);

  /// The RAM disk's file list on the console.  From an lwIP callback only
  /// (or before the stack runs): FatFs is not reentrant.  The server prints
  /// it itself on mount and when the verdict arrives.
  void
  list_files (const char* title);
} // namespace file_server
