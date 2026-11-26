/*
 * clock-test -- the system tick and hrclock, against the platform timer.
 *
 * The other ports' clock-test (native: hrclock's origin against
 * CLOCK_MONOTONIC; pico2 / rpi: the tick against the board's free-running
 * counter). Here the independent reference is `rdtime`: the counter the
 * port derives its tick deadlines from, read directly, never through the
 * kernel.
 *
 *   1. sysclock.sleep_for (100) lasts 100 ms of rdtime        (+-2 ms)
 *   2. 1000 sysclock ticks are 1 s of rdtime                    (+-0.5 %)
 *   3. hrclock.sleep_until (now + 100 ms) lasts 100 ms of rdtime  (+-2 ms)
 *   4. hrclock.now () is within 2 ms of the tick-derived steady count
 *   5. the CLINT's mtime - rdtime offset the port measured is reported; on
 *      the C906 it is the cv1800 bring-up's open question (expected 0)
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>
#include <cmsis-plus/diag/trace.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include <board.hpp>
#include <hw_result.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

using namespace os::rtos;

extern "C" volatile std::int64_t riscv_port_clint_offset;

namespace
{
  constexpr std::uint64_t HZ = riscv_board::TIMEBASE_HZ;

  std::uint64_t
  us_of (std::uint64_t ticks)
  {
    return ticks * 1000000ULL / HZ;
  }

  void
  line (const char* s)
  {
    uart::write (s);
  }

  bool
  check (const char* what, std::uint64_t got_us, std::uint64_t want_us,
         std::uint64_t tol_us)
  {
    const bool ok = got_us + tol_us >= want_us && got_us <= want_us + tol_us;
    char b[128];
    std::snprintf (b, sizeof (b), "  %-40s %8" PRIu64 " us (want %" PRIu64
                                  " +- %" PRIu64 ")  %s\n",
                   what, got_us, want_us, tol_us, ok ? "ok" : "FAIL");
    line (b);
    return ok;
  }
} // namespace

int
os_main (int, char*[])
{
  uart::init (board::UART_BAUD);
  line ("\nclock-test on " PORT_BANNER_LONG "\n");

  bool ok = true;

  // Let the tick settle first (the first period is partial).
  sysclock.sleep_for (2);

  // 1. One sleep.
  {
    const std::uint64_t t0 = riscv::csr::rdtime ();
    sysclock.sleep_for (100);
    const std::uint64_t t1 = riscv::csr::rdtime ();
    ok &= check ("sysclock.sleep_for (100 ms)", us_of (t1 - t0), 100000, 2000);
  }

  // 2. The tick rate over a full second.
  {
    sysclock.sleep_for (1); // align to a tick
    const clock::timestamp_t k0 = sysclock.now ();
    const std::uint64_t t0 = riscv::csr::rdtime ();
    sysclock.sleep_for (1000);
    const std::uint64_t t1 = riscv::csr::rdtime ();
    const clock::timestamp_t k1 = sysclock.now ();
    char b[96];
    std::snprintf (b, sizeof (b), "  %" PRIu64 " ticks counted\n",
                   static_cast<std::uint64_t> (k1 - k0));
    line (b);
    ok &= check ("1000 ticks of sysclock", us_of (t1 - t0), 1000000, 5000);
    ok &= (k1 - k0) >= 1000 && (k1 - k0) <= 1001;
  }

  // 3. hrclock: its units are the timebase.
  {
    const std::uint64_t t0 = riscv::csr::rdtime ();
    hrclock.sleep_until (hrclock.now () + HZ / 10);
    const std::uint64_t t1 = riscv::csr::rdtime ();
    ok &= check ("hrclock.sleep_until (now + 100 ms)", us_of (t1 - t0), 100000,
                 2000);
  }

  // 4. hrclock.now () against the tick-derived steady count.
  {
    const std::uint64_t now = hrclock.now ();
    const std::uint64_t steady = hrclock.steady_now ();
    const std::uint64_t gap = now > steady ? now - steady : steady - now;
    ok &= check ("|hrclock.now - steady_now|", us_of (gap), 0, 2000);
  }

  // 5. The measured CLINT offset.
  {
    char b[96];
    std::snprintf (b, sizeof (b), "  CLINT mtime - rdtime offset: %" PRId64
                                  " ticks\n",
                   static_cast<std::int64_t> (riscv_port_clint_offset));
    line (b);
  }

  if (ok)
    {
      line ("RESULT: PASS (clock-test)\n");
      hw_result::ok ();
    }
  line ("RESULT: FAIL (clock-test)\n");
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}
