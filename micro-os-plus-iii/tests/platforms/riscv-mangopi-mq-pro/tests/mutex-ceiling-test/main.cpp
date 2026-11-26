// mutex-ceiling-test: a protect-protocol lock refused with EINVAL leaves
// nothing behind.
//
// Carried over from the native (posix-arch) board. Only the console calls
// differ (uart::write here); the native start-up scaffolding is replaced by
// this port's own start-up and the kernel's main().
//
// A thread whose priority is above a protect mutex's ceiling must get EINVAL
// from lock() and must not own the mutex. mutex::internal_try_lock_() used to
// link the mutex into the thread's list of owned mutexes (and count it) before
// the ceiling check, and the EINVAL path cleared only owner_. The refused
// thread then exited "owning" it: internal_exit_() asserts that list is empty,
// and internal_destroy_() marked the (robust) mutex owner-dead, so the next
// rightful lock() returned EOWNERDEAD.
//
// Checks: the refused lock is EINVAL; after the refused thread exits and is
// destroyed, a thread under the ceiling locks and unlocks normally, twice.

#include <cmsis-plus/rtos/os.h>
#include <uart.hpp>
#include <hw_result.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace os;
using namespace os::rtos;

namespace
{
  mutex* g_mx = nullptr;
  result_t g_high_res = result::ok;
  result_t g_low_lock = result::ok;
  result_t g_low_unlock = result::ok;

  unsigned g_failures = 0;

  void
  check (bool cond, const char* what, result_t res)
  {
    char b[160];
    std::snprintf (b, sizeof (b), "  %-48s res=%3u  %s\n", what,
                   static_cast<unsigned> (res), cond ? "ok" : "FAIL");
    uart::write (b);
    if (!cond)
      {
        ++g_failures;
      }
  }

  void*
  high_func (void*)
  {
    g_high_res = g_mx->lock ();
    return nullptr;
  }

  void*
  low_func (void*)
  {
    g_low_lock = g_mx->lock ();
    g_low_unlock = g_mx->unlock ();
    return nullptr;
  }
} // namespace

int
os_main (int, char*[])
{
  uart::init (115200);
  uart::write ("\nmutex-ceiling-test: a refused protect lock leaves nothing\n");

  mutex::attributes ma;
  ma.mx_protocol = mutex::protocol::protect;
  ma.mx_robustness = mutex::robustness::robust;
  ma.mx_priority_ceiling = thread::priority::normal;
  static mutex mx{ "mx", ma };
  g_mx = &mx;

  // Above the ceiling: lock() must be refused.
  thread::attributes ha;
  ha.th_priority = thread::priority::above_normal;
  thread* high = new thread{ "high", high_func, nullptr, ha };
  high->join ();
  delete high;
  check (g_high_res == EINVAL, "lock above the ceiling is EINVAL", g_high_res);

  // At the ceiling, from a fresh thread and from main: plain lock/unlock.
  thread::attributes la;
  la.th_priority = thread::priority::normal;
  thread* low = new thread{ "low", low_func, nullptr, la };
  low->join ();
  delete low;
  check (g_low_lock == result::ok, "lock at the ceiling after it (thread)",
         g_low_lock);
  check (g_low_unlock == result::ok, "unlock (thread)", g_low_unlock);

  result_t r = mx.lock ();
  check (r == result::ok, "lock at the ceiling (main)", r);
  r = mx.unlock ();
  check (r == result::ok, "unlock (main)", r);

  const bool ok = (g_failures == 0);
  uart::write (ok ? "RESULT: PASS\n" : "RESULT: FAIL\n");
  if (ok)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }
  return ok ? 0 : 1;
}
