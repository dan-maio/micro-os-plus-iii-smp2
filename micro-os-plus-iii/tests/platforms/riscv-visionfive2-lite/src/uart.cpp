/*
 * uart.cpp -- VisionFive 2 Lite console on UART0 (DesignWare APB 8250, 32-bit
 * registers, reg-shift 2).
 *
 * The divisor is NOT reprogrammed: the SPL / U-Boot set UART0 to 115200 8N1
 * (stdout-path = "serial0:115200n8") and that is what the terminal works
 * with. The driver only makes sure the line format and FIFOs are as expected.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstddef>
#include <cstdint>

#include <board.hpp>
#include <uart.hpp>

#if defined(HW_BUILD) && defined(SEMIHOST)
#include <semihosting.hpp>
#include <riscv-hart.hpp>
#endif

namespace
{
  constexpr std::uintptr_t UART0_BASE = 0x10000000u;

  constexpr unsigned THR = 0x00; // transmit holding (DLAB=0)
  constexpr unsigned FCR = 0x08; // FIFO control
  constexpr unsigned LCR = 0x0C; // line control
  constexpr unsigned LSR = 0x14; // line status

  constexpr std::uint32_t LSR_THRE = 1u << 5;

  inline volatile std::uint32_t&
  reg (unsigned off)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (UART0_BASE + off);
  }

  void
  raw_put (char c)
  {
    // Bounded wait: a test must not hang because nobody is draining the
    // UART (a closed terminal, or the host not reading), but it must also not
    // drop bytes while a terminal *is* reading: at 115200 baud a full 16-byte
    // FIFO takes ~1.4 ms, so allow ~5 ms of polling before giving up on the
    // byte.  The RAM log keeps the text either way.
    for (unsigned i = 0; i < 500000u; ++i)
      {
        if ((reg (LSR) & LSR_THRE) != 0)
          {
            reg (THR) = static_cast<std::uint8_t> (c);
            return;
          }
      }
  }

  void
  raw_write (const char* s, std::size_t n)
  {
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            raw_put ('\r');
          }
        raw_put (s[i]);
      }
  }

#if defined(HW_BUILD) && defined(SEMIHOST)
  // Only the verdict crosses JTAG.  Copying every console write to
  // semihosting made a 36 ms/142 ms solver take 49 s: the test prints 32
  // matrix lines, each built from many small writes, and each semihosting
  // call halts the hart for a probe round trip.  The Pico 2 board already
  // does this (test/boards/pico2/src/uart.cpp); the VF2 did not.
  bool
  has_verdict (const char* s, std::size_t n)
  {
    static constexpr char key[] = "RESULT:";
    constexpr std::size_t k = sizeof (key) - 1;
    for (std::size_t i = 0; i + k <= n; ++i)
      {
        std::size_t j = 0;
        while (j < k && s[i + j] == key[j])
          {
            ++j;
          }
        if (j == k)
          {
            return true;
          }
      }
    return false;
  }

  // SYS_WRITE (buffer + length): OpenOCD fetches the whole buffer in one
  // memory read.  SYS_WRITE0 makes it read the string one byte per probe
  // round trip; on this board's DAPLink (CMSIS-DAP fw 0255, HID, USB full
  // speed) that traffic killed the JTAG link mid-run twice.  Keep SYS_WRITE,
  // but only for the RESULT line.
  // Not from a trap: each semihosting call halts the hart for a JTAG round
  // trip, far longer than a tick.  Output written by a handler (a timer
  // callback's printf) goes to the UART only.
  void
  semi_copy (const char* s, std::size_t n)
  {
    if (n == 0 || !semihosting::attached () || riscv::in_trap ()
        || !has_verdict (s, n))
      {
        return;
      }
    semihosting::write_buf (s, n);
  }
#endif
} // namespace

namespace uart
{
  void
  init (std::uint32_t baud)
  {
    (void) baud; // U-Boot's divisor is kept, see above
    reg (LCR) = 0x03; // 8N1, DLAB=0
    reg (FCR) = 0x07; // FIFOs on, both cleared
  }

  void
  put_char (char c)
  {
    raw_put (c);
  }

  void
  write (const char* s)
  {
    std::size_t n = 0;
    while (s[n] != '\0')
      {
        ++n;
      }
    write (s, n);
  }

  void
  write (const char* s, std::size_t n)
  {
    raw_write (s, n);
#if defined(HW_BUILD) && defined(SEMIHOST)
    semi_copy (s, n);
#endif
  }
} // namespace uart

extern "C" void
riscv_board_console_write (const char* s, std::size_t n)
{
  // If the running test claimed the RAM console log (magic 'VF2L' at
  // board::SCRATCH_BASE), mirror everything the kernel's trace, the fault
  // report and the test itself print into it.  The log is what a JTAG dump
  // can read back; without the magic this is a plain UART write.
  constexpr std::uintptr_t base = board::SCRATCH_BASE;
  constexpr std::uint32_t magic = 0x5646324Cu; // "VF2L"
  constexpr std::uint32_t capacity = 48u * 1024u;

  auto& hdr_magic = *reinterpret_cast<volatile std::uint32_t*> (base);
  auto& hdr_len = *reinterpret_cast<volatile std::uint32_t*> (base + 4u);
  auto* text = reinterpret_cast<volatile char*> (base + 8u);

  if (hdr_magic == magic)
    {
      for (std::size_t i = 0; i < n; ++i)
        {
          const std::uint32_t at = hdr_len;
          if (at + 1u < capacity)
            {
              text[at] = s[i];
              text[at + 1u] = '\0';
              __asm__ volatile("" ::: "memory");
              hdr_len = at + 1u;
            }
        }
    }
  uart::write (s, n);
}
