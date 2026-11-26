/*
 * semihosting-test -- the RISC-V semihosting channel, end to end.
 *
 * Every verdict of this port reaches the runner through semihosting (the
 * RESULT line on SYS_WRITE0, the exit code on SYS_EXIT), so the channel is
 * tested on its own rather than only as a side effect:
 *
 *   SYS_WRITEC   one character                   (write_char)
 *   SYS_WRITE0   a string, one longer than a FIFO  (write_str)
 *   SYS_WRITE    a buffer that is not NUL-terminated (write_buf)
 *   SYS_TIME     the host's clock answers (non-zero), so the call RETURNED
 *                a value and a0 came back through the trap
 *   SYS_ERRNO    a second value-returning call
 *   from a thread, with the tick running: semihosting halts the hart for the
 *   debugger, and the tick must survive the pause
 *   SYS_EXIT     the verdict itself
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <cstdint>
#include <cstdio>

#include <board.hpp>
#include <hw_result.hpp>
#include <semihosting.hpp>
#include <uart.hpp>

using namespace os::rtos;

namespace
{
  constexpr long SYS_TIME = 0x11;
  constexpr long SYS_ERRNO = 0x13;
} // namespace

int
os_main (int, char*[])
{
  uart::init (board::UART_BAUD);
  uart::write ("\nsemihosting-test on " PORT_BANNER_LONG "\n");

  if (!semihosting::attached () && semihosting::call (SYS_TIME, nullptr) < 0)
    {
      uart::write ("no debugger/emulator semihosting -- nothing to test\n");
      uart::write ("RESULT: FAIL (semihosting-test: not attached)\n");
      hw_result::fail ();
    }

  semihosting::write_char ('[');
  semihosting::write_char ('c');
  semihosting::write_char (']');
  semihosting::write_char ('\n');
  semihosting::write_str ("semihosting SYS_WRITE0 from a RISC-V hart\n");
  semihosting::write_str (
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
      "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n");

  semihosting::write_buf ("SYS_WRITE: 13 bytes of this", 13);
  semihosting::write_char ('\n');

  const long t = semihosting::call (SYS_TIME, nullptr);
  const long e = semihosting::call (SYS_ERRNO, nullptr);

  // Ten calls spread over 100 ms of ticks.
  const auto k0 = sysclock.now ();
  long t_last = t;
  for (int i = 0; i < 10; ++i)
    {
      sysclock.sleep_for (10);
      t_last = semihosting::call (SYS_TIME, nullptr);
    }
  const auto dk = sysclock.now () - k0;

  char b[128];
  std::snprintf (b, sizeof (b), "SYS_TIME=%ld (then %ld) SYS_ERRNO=%ld ticks=%lu\n",
                 t, t_last, e, static_cast<unsigned long> (dk));
  uart::write (b);

  // SYS_ERRNO: any value. It is the host's last errno, and its initial value
  // is the host's choice: QEMU 0, OpenOCD -1 (semihosting_common.c sets
  // sys_errno = -1 at start). The call returning at all is what is tested.
  (void) e;
  const bool ok = t > 0 && t_last >= t && dk >= 100;
  if (ok)
    {
      uart::write ("RESULT: PASS (semihosting-test)\n");
      hw_result::ok ();
    }
  uart::write ("RESULT: FAIL (semihosting-test)\n");
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}
