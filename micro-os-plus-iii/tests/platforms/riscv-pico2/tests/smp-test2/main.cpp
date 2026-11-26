/*
 * smp-test2 (Pico 2 / RP2350, Hazard3) -- dual-hart SMP RTOS.
 *
 * Ported from the Cortex-M Pico 2 test of the same name (micro-os-plus-iii-
 * cortexm, test/pico2/smp-test2); only the architecture-specific lines
 * changed. The shared micro-os-plus-iii kernel runs in its SMP variant on
 * BOTH harts (OS_USE_SMP_SCHEDULER + OS_NCPU 2), on the RISC-V port:
 *   - per-hart ecall context switch + per-hart machine timer (hart 0 ticks
 *     the RTOS clock, hart 1 only reschedules);
 *   - affinity scheduler (worker0 pinned to hart 0, worker1 to hart 1);
 *   - kernel spinlock (an AMO, recursive) protecting the shared data;
 *   - hart 1 launched into port_smp_secondary_start (QEMU: released from
 *     its park in startup.S; RP2350: the boot ROM FIFO handshake).
 *
 * Proves:
 *   - both cores running the scheduler (each worker prints its SIO_CPUID);
 *   - cross-core shared state (global counter under a mutex);
 *   - system clock (1 ms tick, advanced by core 0);
 *   - LED heartbeat on core 0.
 *
 * Output on UART0 @115200. The single-core Phase-2 test is smp-test1
 * (unchanged, no OS_USE_SMP_SCHEDULER).
 */

#include <cmsis-plus/rtos/os.h>

#include <led.hpp>
#include <multicore.hpp>
#include <uart.hpp>

#include <hw_result.hpp>
#include <riscv-smp.hpp>

#include <cstdio>
#include <cstdint>

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
  // Bounded run: each worker does kIters beats (~kIters * 500 ms), then the
  // main thread reports the verdict.
  constexpr unsigned kIters = 20;

  unsigned g_global = 0;
  volatile unsigned g_done = 0;
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
    char b[96];
    int n = std::snprintf (
        b, sizeof (b), "[%8lu ms] %-6s #%d core=%u local=%-4u global=%u\n",
        static_cast<unsigned long> (sysclock.now ()), role, id,
        port_cpu_id (), local, global);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  void*
  worker (void* arg)
  {
    auto* c = static_cast<ctx*> (arg);
    // Pin this thread to its core (also set in os_main). A freshly created
    // thread has default all-core affinity and could briefly be picked by
    // the wrong core before os_main calls cpu_affinity(); pinning here and
    // yielding hands it to the right core before any work is done.
    this_thread::thread ().cpu_affinity (1u << c->id);
    if (port_cpu_id () != static_cast<unsigned> (c->id))
      {
        this_thread::yield ();
      }
    for (unsigned i = 0; i < kIters; ++i)
      {
        c->local++;
        g_mutex.lock ();
        unsigned g = ++g_global;
        g_mutex.unlock ();
        line ("worker", c->id, c->local, g);
        g_beats.post (); // signal the LED thread
        sysclock.sleep_for (500);
      }
    g_done++;
    return nullptr;
  }

  void*
  led_thread (void*)
  {
    bool on = false;
    for (;;)
      {
        g_beats.wait (); // one blink per worker beat
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

  // Hart-1 idle thread (registered in os_idle_thread_core[1] before the
  // launch; the kernel's is_thread_allowed_on_cpu pins it to hart 1 by the
  // reserved "idle1" name). No wfi on this port: it yields.
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
}

int
os_main (int /*argc*/, char* /*argv*/[])
{
  // Pin the main thread (which launches core 1 and owns os_main) to core 0:
  // its default affinity is all cores, so core 1's affinity picker could
  // otherwise grab it mid-launch.
  this_thread::thread ().cpu_affinity (1u << 0);

  led::init ();

  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350 (Hazard3) - micro-os-plus-iii\n");
  uart::write ("  smp-test2 : dual-core SMP RTOS\n");
  uart::write ("=============================================\n");

  // Core-1 idle thread + registration.
  static thread::stack::element_t idle1_stack[512];
  static thread* idle1_thread_ptr;
  {
    thread::attributes attr = thread::initializer;
    attr.th_stack_address = idle1_stack;
    attr.th_stack_size_bytes = sizeof (idle1_stack);
    idle1_thread_ptr = new thread ("idle1", secondary_idle_func, nullptr,
                                   attr);
    os::rtos::scheduler::os_idle_thread_core[1] = idle1_thread_ptr;
  }

  uart::write ("core 0: idle1 installed; creating worker/LED threads...\n");

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
      // Pin worker 0 to core 0 and worker 1 to core 1.
      workers[i]->cpu_affinity (1u << i);
    }

  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };
  led_th.cpu_affinity (1u << 0);

  uart::write ("core 0: launching core 1...\n");
  multicore::launch_core1 (port_smp_secondary_start,
                           port_core1_stack_top ());
  port_smp_core0_ipi_enable ();
  uart::write ("core 0: core 1 launched; SMP running.\n");

  // Wait for both bounded workers to finish (or time out).
  for (unsigned i = 0; (g_done < 2u) && (i < 200u); ++i)
    {
      sysclock.sleep_for (100);
    }

  const bool ok = (g_done >= 2u) && (g_global >= 2u * kIters)
                  && (worker_ctx[0].local >= kIters)
                  && (worker_ctx[1].local >= kIters);
  char b[96];
  int n = std::snprintf (
      b, sizeof (b), "\nRESULT: %s  (global=%u wrk0=%u wrk1=%u done=%u)\n",
      ok ? "PASS" : "FAIL", g_global, worker_ctx[0].local, worker_ctx[1].local,
      static_cast<unsigned> (g_done));
  uart::write (b, static_cast<std::size_t> (n));

  // Ends through the strong _Exit() -> semihosting SYS_EXIT; hw_result also
  // reports RESULT on the semihosting console the runner reads.
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
      sysclock.sleep_for (1000);
    }
  return 0;
}
