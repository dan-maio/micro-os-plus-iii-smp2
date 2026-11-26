/*
 * clock-catchup -- the RTOS clock follows real time across missed ticks.
 *
 * A thread masks interrupts and busy-waits 100 ms on mtime (clint::now), ten times (one
 * second in which the tick cannot be taken, as during a debugger halt or a
 * long critical section), then sleeps a while. At the end sysclock must
 * agree with mtime within 2 %: the port's tick (os-core.cpp
 * riscv_port_tick) processes every period mtime has passed, at most 8 per
 * trap, the rest on the following ticks.
 *
 * Measured on the Duo before the catch-up: smp-test-nested's sysclock ran
 * 19 s while rdtime and a DW timer ran 45 s (semihosting halts).
 */

#include <cmsis-plus/rtos/os.h>

#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include <cstdint>

#include <clint.hpp>
#include <cstdio>

using namespace os::rtos;

namespace
{
  std::uint64_t
  ms_mtime (void)
  {
    return clint::now () / (riscv_board::TIMEBASE_HZ / 1000u);
  }

  void
  masked_busy_ms (unsigned ms)
  {
    const riscv::reg_t s = riscv::csr::mstatus_clear_mie ();
    const std::uint64_t t0 = clint::now ();
    while (clint::now () - t0
           < static_cast<std::uint64_t> (ms) * (riscv_board::TIMEBASE_HZ / 1000u))
      {
      }
    if (s)
      {
        riscv::csr::mstatus_set_mie ();
      }
  }
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  uart::write ("\nclock-catchup: sysclock vs mtime across masked periods\n");

  const auto s0 = sysclock.now ();
  const std::uint64_t r0 = ms_mtime ();

  for (int i = 0; i < 10; ++i)
    {
      masked_busy_ms (100);
      sysclock.sleep_for (20);
    }
  sysclock.sleep_for (300); // time for the backlog to be worked off

  const auto ds = static_cast<std::uint64_t> (sysclock.now () - s0);
  const std::uint64_t dr = ms_mtime () - r0;
  const std::uint64_t diff = (ds > dr) ? ds - dr : dr - ds;
  const bool ok = diff * 50u <= dr; // within 2 %

  char b[120];
  int n = std::snprintf (b, sizeof (b),
                         "RESULT: %s  (sysclock %lu ms, mtime %lu ms)\n",
                         ok ? "PASS" : "FAIL", static_cast<unsigned long> (ds),
                         static_cast<unsigned long> (dr));
  uart::write (b, static_cast<std::size_t> (n));
  if (ok)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }
  return 0;
}
