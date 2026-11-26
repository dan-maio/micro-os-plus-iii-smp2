/*
 * smp-test1 (WCH CH32V307V-EVT-R1, QingKe V4F) -- the single-core RTOS
 * bring-up of the Pico 2's smp-test1 (test/pico2/smp-test1), the first
 * kernel test on the port's PFIC path: the software interrupt switches
 * threads, SysTick is the tick, critical sections are the PFIC threshold.
 * Only the banner differs from the Pico 2's.
 */

#include <cmsis-plus/rtos/os.h>

#include <uart.hpp>
#include <led.hpp>
#include <hw_result.hpp>

#include <cstdio>
#include <cstdint>

using namespace os::rtos;

namespace
{
  unsigned g_global = 0;
  mutex g_mutex{ "g" };
  semaphore_counting g_beats{ "beats", 8, 0 };

  thread::stack::element_t worker_stacks[2][1024];
  thread::stack::element_t led_stack[1024];

  struct ctx
  {
    int id;
    unsigned local;
  };
  ctx worker_ctx[2];

  void
  line (const char* role, int id, unsigned local, unsigned global)
  {
    char b[80];
    int n = std::snprintf (
        b, sizeof (b), "[%8lu ms] %-6s #%d  local=%-4u global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), role, id, local, global);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  void*
  worker (void* arg)
  {
    auto* c = static_cast<ctx*> (arg);
    for (;;)
      {
        c->local++;
        g_mutex.lock ();
        unsigned g = ++g_global;
        g_mutex.unlock ();
        line ("worker", c->id, c->local, g);
        g_beats.post (); // signal the LED thread
        sysclock.sleep_for (500);
      }
    return nullptr;
  }

  void*
  led_thread (void*)
  {
    // One blink per worker beat. The two workers run in lockstep, so their
    // beats arrive ~2 ms apart; without a minimum ON time the LED would
    // toggle ON/OFF almost instantly and be barely visible. Each beat now
    // produces a 100 ms visible pulse.
    for (;;)
      {
        g_beats.wait (); // one blink per worker beat
        led::on ();
        sysclock.sleep_for (100);
        led::off ();
      }
    return nullptr;
  }
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  WCH CH32V307V-EVT-R1 - micro-os-plus-iii\n");
  uart::write ("  smp-test1 : single-core RTOS (PFIC path)\n");
  uart::write ("=============================================\n");

  thread::attributes attr = thread::initializer;

  static thread* workers[2];
  for (int i = 0; i < 2; ++i)
    {
      worker_ctx[i] = ctx{ i, 0 };
      attr.th_stack_address = worker_stacks[i];
      attr.th_stack_size_bytes = sizeof (worker_stacks[i]);
      char name[8];
      std::snprintf (name, sizeof (name), "wrk%d", i);
      workers[i] = new thread (name, worker, &worker_ctx[i], attr);
    }

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };

  // Let the two workers and the LED thread run for ten beats, then check that
  // the scheduler, the mutex and the system clock all did their job. The
  // verdict is a RESULT line and, under QEMU, a semihosting exit.
  constexpr unsigned kCheckBeats = 10;
  for (unsigned b = 0; b < kCheckBeats; ++b)
    {
      sysclock.sleep_for (500);
    }

  const bool ok = (g_global > 0 && worker_ctx[0].local > 0
                   && worker_ctx[1].local > 0);
  char b[80];
  int n = std::snprintf (b, sizeof (b),
                         "\nRESULT: %s  (global=%u, wrk0=%u, wrk1=%u)\n",
                         ok ? "PASS" : "FAIL", g_global, worker_ctx[0].local,
                         worker_ctx[1].local);
  uart::write (b, static_cast<std::size_t> (n));

  // QEMU runs end here, through the semihosting exit that carries the
  // verdict; on hardware this is a no-op and the idle loop below runs.
  if (ok)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }

  for (;;)
    {
      sysclock.sleep_for (10000);
    }
  return 0;
}
