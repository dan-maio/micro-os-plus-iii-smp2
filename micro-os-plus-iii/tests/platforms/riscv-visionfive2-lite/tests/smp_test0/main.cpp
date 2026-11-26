/*
 * smp_test0 -- VisionFive 2 Lite µOS++ Phase-1 bring-up.
 *
 * On every other port, smp_test0 is the single-core phase of the SMP
 * bring-up: the console, the 1 ms tick and the scheduler running a thread
 * that sleeps on the system clock, with the other cores still parked. On
 * the board that IS the whole system -- µOS++ runs on one U74 hart --
 * so this is the same test, with the hart id read from mhartid and the LED
 * toggled on every beat. The start-up scaffolding the other boards carry in
 * this file is the port's own here (src/startup.*, the kernel's main()).
 *
 * Exactly 10 heartbeats prove the tick and the scheduler, then the run ends
 * through the semihosted exit.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <cstdint>
#include <cstdio>

#include <board.hpp>
#include <hw_result.hpp>
#include <led.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

int
os_main (int, char*[])
{
  uart::init (board::UART_BAUD);
  led::init ();

  const unsigned hart = static_cast<unsigned> (RISCV_CSR_READ (mhartid));
  char b[96];
  std::snprintf (b, sizeof (b),
                 "\n" PORT_BANNER_LONG " - µOS++ SMP TEST 0\n"
                 "Scheduler started on hart %u. Heartbeat via "
                 "sysclock.sleep_for(1000):\n",
                 hart);
  uart::write (b);

  unsigned toggles = 0;
  for (std::uint32_t beat = 0; beat < 10; ++beat)
    {
      led::toggle ();
      ++toggles;
      std::snprintf (b, sizeof (b), "[tick] heartbeat %u  os_clock=%u ms  led=%s\n",
                     static_cast<unsigned> (beat),
                     static_cast<unsigned> (os::rtos::sysclock.now ()),
                     led::is_on () ? "on" : "off");
      uart::write (b);
      os::rtos::sysclock.sleep_for (1000);
    }
  led::off ();

  const bool ok = (os::rtos::sysclock.now () >= 10000) && (toggles == 10);
  std::snprintf (b, sizeof (b), "RESULT: %s (10 heartbeats on hart %u)\n",
                 ok ? "PASS" : "FAIL", hart);
  uart::write (b);
  if (ok)
    {
      hw_result::ok ();
    }
  hw_result::fail ();
  for (;;)
    {
      os::rtos::sysclock.sleep_for (1000);
    }
}
