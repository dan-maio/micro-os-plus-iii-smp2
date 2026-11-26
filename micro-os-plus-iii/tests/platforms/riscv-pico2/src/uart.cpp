/*
 * uart.cpp -- UART0 (PL011) console on GPIO0/GPIO1, 115200 8N1, polled.
 *
 * The register sequence is the Cortex-M pico2 board's (glue/uart.cpp). Both
 * harts may print: a write holds SIO hardware spinlock 1 with this hart's
 * interrupts masked (the Cortex-M board did the same with PRIMASK).
 *
 * Under HW_BUILD + SEMIHOST only the VERDICT reaches semihosting: a console
 * write containing "RESULT:" is copied there (from threads only), the rest
 * stays on the UART. Measured: with every line copied, OpenOCD + the Debug
 * Probe (firmware 2.0.1) stopped servicing semihosting in long runs -- the
 * CMSIS-RTOS validator stalled at its 44th write with core 0 halted on a
 * valid SYS_WRITE0, smp-mat-test lost the debug module after ~114 lines.
 * The trap report goes to semihosting directly (src/trap.cpp).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstddef>
#include <cstdint>

#include <clocks.hpp>
#include <rp2350.hpp>
#include <riscv-csr.hpp>
#include <riscv-hart.hpp>
#include <uart.hpp>

#if defined(HW_BUILD) && defined(SEMIHOST)
#include <semihosting.hpp>
#endif

using rp2350::reg;

namespace
{
  constexpr std::uint32_t DR = 0x00, FR = 0x18, IBRD = 0x24, FBRD = 0x28,
                          LCRH = 0x2C, CR = 0x30, IMSC = 0x38, ICR = 0x44;
  constexpr std::uint32_t FR_TXFF = 1u << 5, FR_RXFE = 1u << 4;
  constexpr std::uint32_t GPIO_FUNC_UART0 = 2;
  constexpr std::uint32_t PADS_ISO = 1u << 8, PADS_IE = 1u << 6;
  constexpr std::uint32_t RESET_UART0 = 1u << 26, RESET_IO_BANK0 = 1u << 6,
                          RESET_PADS_BANK0 = 1u << 9;

  riscv::reg_t
  lock (void)
  {
    const riscv::reg_t s = riscv::csr::mstatus_clear_mie ();
    while (reg (rp2350::UART_SPINLOCK) == 0)
      {
      }
    return s;
  }

  void
  unlock (riscv::reg_t s)
  {
    reg (rp2350::UART_SPINLOCK) = 1;
    if (s)
      {
        riscv::csr::mstatus_set_mie ();
      }
  }

  void
  raw_put (char c)
  {
    while (reg (rp2350::UART0_BASE + FR) & FR_TXFF)
      {
      }
    reg (rp2350::UART0_BASE + DR)
        = static_cast<std::uint32_t> (static_cast<unsigned char> (c));
  }

#if defined(HW_BUILD) && defined(SEMIHOST)
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

  void
  semi_copy (const char* s, std::size_t n)
  {
    if (!semihosting::attached () || riscv::in_trap () || !has_verdict (s, n))
      {
        return;
      }
    char chunk[128];
    std::size_t i = 0;
    while (i < n)
      {
        std::size_t k = 0;
        while (i < n && k < sizeof (chunk) - 1)
          {
            chunk[k++] = s[i++];
          }
        chunk[k] = '\0';
        semihosting::write_str (chunk);
      }
  }
#endif
} // namespace

// The uart:: functions are WEAK: a test that brings its own driver
// (smp-test5: interrupt-driven TX) replaces them, as the Cortex-M Pico 2
// board leaves its shared driver out of that test.
namespace uart
{
  void __attribute__ ((weak))
  init (std::uint32_t baud)
  {
    rp2350::unreset_block (RESET_UART0 | RESET_IO_BANK0 | RESET_PADS_BANK0);

    const std::uint32_t tx = rp2350::UART_TX_PIN;
    const std::uint32_t rx = rp2350::UART_RX_PIN;
    reg (rp2350::IO_BANK0_BASE + tx * 8 + 4)
        = (reg (rp2350::IO_BANK0_BASE + tx * 8 + 4) & ~0x1Fu) | GPIO_FUNC_UART0;
    reg (rp2350::IO_BANK0_BASE + rx * 8 + 4)
        = (reg (rp2350::IO_BANK0_BASE + rx * 8 + 4) & ~0x1Fu) | GPIO_FUNC_UART0;
    reg (rp2350::PADS_BANK0_BASE + 0x04 + tx * 4)
        = (reg (rp2350::PADS_BANK0_BASE + 0x04 + tx * 4) & ~PADS_ISO) | PADS_IE;
    reg (rp2350::PADS_BANK0_BASE + 0x04 + rx * 4)
        = (reg (rp2350::PADS_BANK0_BASE + 0x04 + rx * 4) & ~PADS_ISO) | PADS_IE;

    const std::uint32_t u = rp2350::UART0_BASE;
    reg (u + CR) = 0;
    reg (u + ICR) = 0x7FF;
    // PL011: divisor = clk_peri / (16 * baud), FBRD the rounded fraction * 64.
    const std::uint32_t clk = clocks::current_khz () * 1000u;
    const std::uint32_t d16 = 16u * baud;
    std::uint32_t ibrd = clk / d16;
    std::uint32_t fbrd = ((clk % d16) * 64u + d16 / 2u) / d16;
    if (fbrd >= 64u)
      {
        ibrd += 1u;
        fbrd -= 64u;
      }
    reg (u + IBRD) = ibrd;
    reg (u + FBRD) = fbrd;
    reg (u + LCRH) = (3u << 5) | (1u << 4); // 8N1, FIFOs
    reg (u + IMSC) = 0;                     // polled
    reg (u + CR) = (1u << 9) | (1u << 8) | (1u << 0); // RXE | TXE | UARTEN
  }

  void __attribute__ ((weak))
  put_char (char c)
  {
    const riscv::reg_t s = lock ();
    raw_put (c);
    unlock (s);
  }

  void __attribute__ ((weak))
  write (const char* s)
  {
    std::size_t n = 0;
    while (s[n] != '\0')
      {
        ++n;
      }
    write (s, n);
  }

  void __attribute__ ((weak))
  write (const char* s, std::size_t n)
  {
    const riscv::reg_t st = lock ();
    for (std::size_t i = 0; i < n; ++i)
      {
        if (s[i] == '\n')
          {
            raw_put ('\r');
          }
        raw_put (s[i]);
      }
    unlock (st);
#if defined(HW_BUILD) && defined(SEMIHOST)
    semi_copy (s, n);
#endif
  }

  int __attribute__ ((weak))
  get_char (void)
  {
    if (reg (rp2350::UART0_BASE + FR) & FR_RXFE)
      {
        return -1;
      }
    return static_cast<int> (reg (rp2350::UART0_BASE + DR) & 0xFFu);
  }
} // namespace uart

extern "C" void
riscv_board_console_write (const char* s, std::size_t n)
{
  uart::write (s, n);
}
