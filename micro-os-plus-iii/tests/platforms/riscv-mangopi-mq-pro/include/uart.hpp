/*
 * uart.hpp -- the MangoPi MQ-Pro console: Allwinner D1 UART0.
 *
 *   UART0        0x02500000 (16550-compatible, 32-bit registers)
 *   pins         header pin 8 = PB8 = UART0_TX (mux 6 in PB_CFG1)
 *                header pin 10 = PB9 = UART0_RX
 *   clock        APB1 = HOSC / 1 = 24 MHz (CCU + 0x524)
 *   divisor      24 MHz / (16 * 115200) = 13  -> 115384 Bd
 *
 * The D1 UART's baud clock is APB1, not a dedicated UART PLL, and the UART
 * has an integer divisor only.  The driver sets the clock, pin mux, bus
 * gate/reset and divisor itself -- xfel leaves no console configured.
 *
 * Polled, blocking transmit.  Unlike the Duo, the hardware runner reads the
 * verdict from this UART, not from semihosting, so no copy is made there;
 * test/<board>/include/hw_result.hpp ends the run.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstddef>
#include <cstdint>

// The board contract used by the tests' banners.
#define PORT_BANNER_LONG "MangoPi MQ-Pro (Allwinner D1, T-Head C906, RV64GC)"
#define PORT_BANNER_SHORT "MangoPi MQ-Pro"
#define PORT_BANNER_ISA "RV64GC"
#define PORT_BANNER_CPU "C906"

namespace uart
{
  // Program 8N1 at `baud` from the 24 MHz APB1 clock.  0 keeps 115200.
  void
  init (std::uint32_t baud);

  void
  put_char (char c);

  void
  write (const char* s);

  // Exactly n bytes; '\n' is sent as "\r\n".
  void
  write (const char* s, std::size_t n);

  // Wait until the transmitter (FIFO and shift register) is empty, with a
  // bounded timeout.  Used before the test resets the SoC so the RESULT
  // line cannot be cut off.
  void
  flush (void);
} // namespace uart
