/*
 * smp-test2 (VisionFive 2 Lite) -- SMP RTOS on the four U74 harts.
 *
 * The Pico 2 test of the same name (test/pico2/smp-test2, itself from the
 * Cortex-M Pico 2 port) generalised from two cores to OS_NCPU (4 here). The
 * kernel's SMP scheduler runs on every U74 (CPU n = mhartid n + 1,
 * RISCV_HART_BASE = 1):
 *   - CPUs 1..3 get their idle threads idle1..idle3 (registered in
 *     os_idle_thread_core[] before the launch) and are released from the
 *     port's park (multicore::launch_core);
 *   - one worker per core, pinned by cpu_affinity(); each reads mhartid on
 *     every beat and counts the beats it did on its own core;
 *   - shared state (a global counter) under a mutex, the 1 ms tick on CPU 0,
 *     the LED blinking once per beat from a thread on CPU 0.
 *
 * PASS: every worker finished kIters beats, all of them on its own hart,
 * and the global counter saw every beat.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <led.hpp>
#include <multicore.hpp>
#include <uart.hpp>

#include <hw_result.hpp>
#include <riscv-csr.hpp>
#include <riscv-smp.hpp>

#include <cstdint>
#include <cstdio>

using namespace os::rtos;

namespace os
{
  namespace rtos
  {
    namespace scheduler
    {
      extern thread* os_idle_thread_core[OS_NCPU];
    }
  }
}

namespace
{
  // Each worker does kIters beats, kBeatMs apart. Every beat prints a line,
  // and every line is a semihosting call (~0.8 s with the DAPLink), so the
  // run is kept short.
  constexpr unsigned kIters = 8;
  constexpr unsigned kBeatMs = 500;

  unsigned g_global = 0;
  volatile unsigned g_done = 0;
  mutex g_mutex{ "g" };
  semaphore_counting g_beats{ "beats", 16, 0 };

  thread::stack::element_t worker_stacks[OS_NCPU][1024];
  thread::stack::element_t led_stack[1024];
  thread::stack::element_t idle_stacks[OS_NCPU][512];
  char idle_names[OS_NCPU][8];
  char worker_names[OS_NCPU][8];

  struct ctx
  {
    unsigned id;
    unsigned local;    // beats done
    unsigned on_own;   // beats done on its own hart
    unsigned last_hart;
  };
  ctx worker_ctx[OS_NCPU];

  unsigned
  mhartid (void)
  {
    return static_cast<unsigned> (RISCV_CSR_READ (mhartid));
  }

  void
  line (unsigned id, unsigned local, unsigned global)
  {
    char b[112];
    int n = std::snprintf (
        b, sizeof (b),
        "[%8lu ms] worker #%u cpu=%u mhartid=%u local=%-3u global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), id, port_cpu_id (),
        mhartid (), local, global);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  void*
  worker (void* arg)
  {
    auto* c = static_cast<ctx*> (arg);
    // Pin here too: a new thread has the all-cores affinity until os_main
    // sets it, and could be picked by another core in between.
    this_thread::thread ().cpu_affinity (1u << c->id);
    if (port_cpu_id () != c->id)
      {
        this_thread::yield ();
      }
    for (unsigned i = 0; i < kIters; ++i)
      {
        c->local++;
        c->last_hart = mhartid ();
        if (c->last_hart == c->id + RISCV_HART_BASE)
          {
            c->on_own++;
          }
        g_mutex.lock ();
        unsigned g = ++g_global;
        g_mutex.unlock ();
        line (c->id, c->local, g);
        g_beats.post ();
        sysclock.sleep_for (kBeatMs);
      }
    g_mutex.lock ();
    g_done++;
    g_mutex.unlock ();
    return nullptr;
  }

  void*
  led_thread (void*)
  {
    bool on = false;
    for (;;)
      {
        g_beats.wait ();
        on = !on;
        if (on)
          {
            led::on ();
          }
        else
          {
            led::off ();
          }
      }
    return nullptr;
  }

  // The idle thread of CPUs 1..3. No wfi on this port: it yields.
  void*
  secondary_idle_func (void*)
  {
    this_thread::thread ().priority (thread::priority::idle);
    while (true)
      {
        this_thread::yield ();
      }
    return nullptr;
  }
} // namespace

int
os_main (int, char*[])
{
  // The main thread launches the other cores: it stays on core 0.
  this_thread::thread ().cpu_affinity (1u << 0);

  uart::init (0);
  led::init ();

  char b[128];
  std::snprintf (b, sizeof (b),
                 "\n=============================================\n"
                 "  %s\n  smp-test2 : %u-core SMP RTOS\n"
                 "=============================================\n",
                 PORT_BANNER_SHORT, static_cast<unsigned> (OS_NCPU));
  uart::write (b);

  for (unsigned c = 1; c < OS_NCPU; ++c)
    {
      thread::attributes attr = thread::initializer;
      attr.th_stack_address = idle_stacks[c];
      attr.th_stack_size_bytes = sizeof (idle_stacks[c]);
      std::snprintf (idle_names[c], sizeof (idle_names[c]), "idle%u", c);
      os::rtos::scheduler::os_idle_thread_core[c]
          = new thread (idle_names[c], secondary_idle_func, nullptr, attr);
    }

  thread::attributes attr = thread::initializer;
  for (unsigned i = 0; i < OS_NCPU; ++i)
    {
      worker_ctx[i] = ctx{ i, 0, 0, 0 };
      attr.th_stack_address = worker_stacks[i];
      attr.th_stack_size_bytes = sizeof (worker_stacks[i]);
      std::snprintf (worker_names[i], sizeof (worker_names[i]), "wrk%u", i);
      thread* t = new thread (worker_names[i], worker, &worker_ctx[i], attr);
      t->cpu_affinity (1u << i);
    }

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };
  led_th.cpu_affinity (1u << 0);

  for (unsigned c = 1; c < OS_NCPU; ++c)
    {
      std::snprintf (b, sizeof (b), "core 0: launching core %u (mhartid %u)\n",
                     c, c + RISCV_HART_BASE);
      uart::write (b);
      multicore::launch_core (c, port_smp_secondary_start,
                              port_secondary_stack_top (c));
    }
  port_smp_core0_ipi_enable ();
  uart::write ("core 0: all cores launched; SMP running.\n");

  // Wait for the workers (bounded: the beats plus the semihosting time).
  for (unsigned i = 0; (g_done < OS_NCPU) && (i < 1200u); ++i)
    {
      sysclock.sleep_for (100);
    }

  bool ok = (g_done == OS_NCPU) && (g_global == OS_NCPU * kIters);
  for (unsigned i = 0; i < OS_NCPU; ++i)
    {
      const ctx& c = worker_ctx[i];
      std::snprintf (b, sizeof (b),
                     "  wrk%u: %u beats, %u on its own hart (mhartid %u)\n",
                     i, c.local, c.on_own, i + RISCV_HART_BASE);
      uart::write (b);
      ok = ok && (c.local == kIters) && (c.on_own == kIters);
    }
  std::snprintf (b, sizeof (b),
                 "\nRESULT: %s  (%u cores, global=%u done=%u)\n",
                 ok ? "PASS" : "FAIL", static_cast<unsigned> (OS_NCPU),
                 g_global, static_cast<unsigned> (g_done));
  uart::write (b);

  if (ok)
    {
      hw_result::ok ();
    }
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
  return 0;
}
