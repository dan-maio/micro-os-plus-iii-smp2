/*
 * smp-autostart.cpp -- run every VisionFive 2 Lite test on all four U74s.
 *
 * Compiled into every test of this board (test/visionfive2-lite/src/), but
 * empty unless the test is built with VF2_SMP_AUTOSTART or is a harness
 * suite (UOS_HARNESS_SUITE; tests.cmake): the tests that start the other
 * cores themselves (smp-test2, smp-mat-test) do not get it.
 *
 * vf2_smp_start() does the work (steps 1..3 below). An auto-started test
 * gets it from the os_main() here; a harness suite from its __wrap_os_main()
 * (include/harness-suite.hpp), before the suite runs.
 *
 * For the others, tests.cmake renames the test's own os_main() to
 * vf2_test_os_main() (an option on the test's own sources only, so the
 * kernel's main() still calls the os_main() below) and builds the test with
 * OS_NCPU = 4. This os_main() runs on CPU 0 (the kernel pins the main
 * thread there), and before handing over to the test it:
 *
 *   1. creates the idle threads idle1..idle3 and registers them in
 *      os_idle_thread_core[] (as smp-test2 does);
 *   2. releases CPUs 1..3 (mhartid 2..4) from the port's park onto their own
 *      boot stacks (multicore::launch_core), then enables CPU 0's IPI;
 *   3. waits until each of CPUs 1..3 has run its idle thread, on its own
 *      hart, and reports it. A core that did not start fails the test here:
 *      a test that passed with the other cores parked would not be an SMP
 *      result.
 *
 * The test's threads then have the kernel's default affinity (every core),
 * so they are scheduled on all four CPUs.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#if (defined(VF2_SMP_AUTOSTART) || defined(UOS_HARNESS_SUITE)) \
    && defined(OS_NCPU) && (OS_NCPU > 1)

#include <cmsis-plus/rtos/os.h>

#include <hw_result.hpp>
#include <multicore.hpp>
#include <riscv-csr.hpp>
#include <riscv-smp.hpp>
#include <uart.hpp>

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

#include <vf2-smp-start.hpp>

namespace
{
  thread::stack::element_t idle_stacks[OS_NCPU][512];
  char idle_names[OS_NCPU][8];

  // Set by each secondary's idle thread: how often it ran, and on which hart.
  volatile unsigned idle_runs[OS_NCPU];
  volatile unsigned idle_hart[OS_NCPU];

  // The idle thread of CPUs 1..3. No wfi on this port: it yields.
  void*
  secondary_idle (void* arg)
  {
    const unsigned cpu
        = static_cast<unsigned> (reinterpret_cast<std::uintptr_t> (arg));
    this_thread::thread ().priority (thread::priority::idle);
    for (;;)
      {
        if (port_cpu_id () == cpu)
          {
            idle_hart[cpu]
                = static_cast<unsigned> (RISCV_CSR_READ (mhartid));
            idle_runs[cpu] = idle_runs[cpu] + 1;
          }
        this_thread::yield ();
      }
    return nullptr;
  }
} // namespace

bool
vf2_smp_start (void)
{
  this_thread::thread ().cpu_affinity (1u << 0);
  uart::init (0);

  for (unsigned c = 1; c < OS_NCPU; ++c)
    {
      thread::attributes attr = thread::initializer;
      attr.th_stack_address = idle_stacks[c];
      attr.th_stack_size_bytes = sizeof (idle_stacks[c]);
      std::snprintf (idle_names[c], sizeof (idle_names[c]), "idle%u", c);
      scheduler::os_idle_thread_core[c] = new thread (
          idle_names[c], secondary_idle,
          reinterpret_cast<void*> (static_cast<std::uintptr_t> (c)), attr);
    }
  for (unsigned c = 1; c < OS_NCPU; ++c)
    {
      multicore::launch_core (c, port_smp_secondary_start,
                              port_secondary_stack_top (c));
    }
  port_smp_core0_ipi_enable ();

  // Every secondary must be running its idle thread on its own hart.
  bool ok = false;
  for (unsigned t = 0; (t < 100u) && !ok; ++t)
    {
      ok = true;
      for (unsigned c = 1; c < OS_NCPU; ++c)
        {
          ok = ok && (idle_runs[c] != 0);
        }
      if (!ok)
        {
          sysclock.sleep_for (10);
        }
    }

  char b[96];
  std::snprintf (b, sizeof (b), "\nsmp: %u cores, CPU 0 = mhartid %u",
                 static_cast<unsigned> (OS_NCPU),
                 static_cast<unsigned> (RISCV_CSR_READ (mhartid)));
  uart::write (b);
  for (unsigned c = 1; c < OS_NCPU; ++c)
    {
      const bool on_own = (idle_runs[c] != 0)
                          && (idle_hart[c] == c + RISCV_HART_BASE);
      ok = ok && on_own;
      std::snprintf (b, sizeof (b), ", CPU %u = %s%u", c,
                     on_own ? "mhartid " : "NOT RUNNING, expected mhartid ",
                     c + RISCV_HART_BASE);
      uart::write (b);
    }
  uart::write (ok ? " -- SMP running\n" : "\n");

  if (!ok)
    {
      uart::write ("RESULT: FAIL (smp: a secondary core did not start)\n");
      hw_result::fail ();
    }

  // Let the line leave the FIFO: the test's uart::init() clears it.
  sysclock.sleep_for (20);
  return ok;
}

#if defined(VF2_SMP_AUTOSTART)

// The test's own os_main(), renamed by tests.cmake. The kernel header
// declares os_main() extern "C", and the rename keeps that linkage.
extern "C" int
vf2_test_os_main (int argc, char* argv[]);

int
os_main (int argc, char* argv[])
{
  vf2_smp_start ();
  return vf2_test_os_main (argc, argv);
}

#endif // VF2_SMP_AUTOSTART

#endif // (VF2_SMP_AUTOSTART || UOS_HARNESS_SUITE) && OS_NCPU > 1
