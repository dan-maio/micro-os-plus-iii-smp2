/*
 * smp-test-nested on the Milk-V Duo: the Cortex-M RP2350B test
 * (micro-os-plus-iii-cortexm, test/pico2-rp2350b-psram/smp-test-nested) and
 * its RISC-V Pico 2 port (test/pico2/smp-test-nested), on the C906 hart and
 * the CV1800B's PLIC (test/boards/milkv-duo/src/irq.cpp). One hart: the two
 * workers share it; the interrupt scenario is the original's.
 *
 *   Pico 2 (Xh3irq)                   Duo (PLIC)                   priority
 *   TIMER0 alarm 2, armed 500 us      DW Timer0 (IRQ 79), one-shot  3 (high)
 *     into the UART handler             armed 500 us into it
 *   TIMER0 alarm 3, 1.2495 s          DW Timer1 (IRQ 80), periodic  2
 *   UART0 IRQ pended by software      UART4 (IRQ 48): the trigger   1 (low)
 *                                       thread enables the TX-empty
 *                                       interrupt; the handler stops it
 *
 * The UART handler busy-waits 2 ms (rdtime), so Timer0 must preempt it:
 * t2_in_uart counts Timer0 entries that found the UART handler running.
 * The tick stays below every external interrupt (the dispatcher masks
 * MTIE/MSIE while a handler runs, as CLEARTS does on Hazard3).
 *
 * PASS: both workers did kIters beats, all three interrupts fired, and
 * t2_in_uart > 0 (nesting happened). No trace printer (the original's
 * lock-free trace buffer): the counters are the verdict.
 */

#include <cmsis-plus/rtos/os.h>

#include <duo-irq.hpp>
#include <led.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include <cstdint>
#include <cstdio>

using namespace os::rtos;

// Bring-up switch: 1 = the PLIC set-up only (no interrupt enabled),
// 2 = + Timer1 (periodic, alone), 3 = everything (the test).
#if !defined(DUO_NESTED_STAGE)
#define DUO_NESTED_STAGE 3
#endif
#if !defined(DUO_NESTED_ITERS)
#define DUO_NESTED_ITERS 20
#endif

namespace
{
  inline volatile std::uint32_t&
  reg (std::uintptr_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  // Load a register for its side effect (an EOI, a status clear, a write
  // landing). Not `(void) reg (a)`: in C++ that is no access to a volatile
  // (GCC: "conversion to void will not access object") -- a missing EOI read
  // left Timer1's interrupt asserted for good (measured: an interrupt storm).
  inline void
  touch (std::uintptr_t a)
  {
    const std::uint32_t v = *reinterpret_cast<volatile std::uint32_t*> (a);
    __asm__ volatile("" ::"r"(v));
  }

  // ---- Time: rdtime, 25 MHz on the Duo ------------------------------------
  inline std::uint32_t
  timer_us32 (void)
  {
    return static_cast<std::uint32_t> (riscv::csr::rdtime ()
                                       / (riscv_board::TIMEBASE_HZ / 1000000u));
  }

  // ---- DesignWare timers (0x030A0000; TRM's Timer1 = IRQ 79) ---------------
  constexpr std::uintptr_t TIMERS = 0x030A0000u;
  constexpr std::uintptr_t T_LOAD = 0x00, T_CUR = 0x04, T_CTRL = 0x08,
                           T_EOI = 0x0c;
  constexpr std::uint32_t CTRL_EN = 1u << 0, CTRL_USER = 1u << 1,
                          CTRL_MASK = 1u << 2;
  // The timers' count clock, measured at start-up against rdtime
  // (measure_timer_hz): the TRM's 25 MHz reset default (12.4.3) did not
  // hold -- Timer1 armed for 1.2495 s fired about every 0.54 s.
  std::uint32_t g_timer_hz = 25000000u;

  std::uint32_t
  measure_timer_hz (void)
  {
    constexpr unsigned n = 2; // Timer index 2: not used by the test
    const std::uintptr_t t = 0x030A0000u + 0x14u * n;
    *reinterpret_cast<volatile std::uint32_t*> (t + 0x08) = 0x4; // stop, masked
    *reinterpret_cast<volatile std::uint32_t*> (t + 0x00) = 0xFFFFFFFFu;
    *reinterpret_cast<volatile std::uint32_t*> (t + 0x08) = 0x5; // free-running
    // Let the reload land before the first sample: right after the enable
    // the counter can still read its old value (measured: a timer left
    // running by the previous run gave a wrapped window, "1847285796 Hz").
    {
      const std::uint64_t w = riscv::csr::rdtime ();
      while (riscv::csr::rdtime () - w < riscv_board::TIMEBASE_HZ / 1000u)
        {
        }
    }
    const std::uint64_t r0 = riscv::csr::rdtime ();
    const std::uint32_t c0 = *reinterpret_cast<volatile std::uint32_t*> (t + 0x04);
    while (riscv::csr::rdtime () - r0 < riscv_board::TIMEBASE_HZ / 10u)
      {
      }
    const std::uint32_t c1 = *reinterpret_cast<volatile std::uint32_t*> (t + 0x04);
    const std::uint64_t r1 = riscv::csr::rdtime ();
    // Timer 2 keeps running (free, masked): the end of the run compares its
    // elapsed time with rdtime's and sysclock's.
    const std::uint64_t ticks = static_cast<std::uint32_t> (c0 - c1); // down
    const std::uint64_t hz = ticks * riscv_board::TIMEBASE_HZ / (r1 - r0);
    if (hz < 1000000u || hz > 100000000u)
      {
        uart::write ("DW timer clock measurement out of range; using 25 MHz\n");
        return 25000000u;
      }
    return static_cast<std::uint32_t> (hz);
  }

  inline std::uintptr_t
  timer (unsigned n)
  {
    return TIMERS + 0x14u * n;
  }

  void
  timer_start (unsigned n, std::uint32_t us)
  {
    reg (timer (n) + T_CTRL) = 0;
    reg (timer (n) + T_LOAD) = static_cast<std::uint32_t> (
        static_cast<std::uint64_t> (us) * g_timer_hz / 1000000u);
    reg (timer (n) + T_CTRL) = CTRL_EN | CTRL_USER; // reloads, IRQ unmasked
  }

  void
  timer_stop (unsigned n)
  {
    reg (timer (n) + T_CTRL) = CTRL_MASK;
  }

  // ---- UART4 (DW 8250, reg-shift 2) ----------------------------------------
  constexpr std::uintptr_t UART4 = 0x041C0000u;
  constexpr std::uintptr_t U_IER = 0x04, U_IIR = 0x08;
  constexpr std::uint32_t IER_ETBEI = 1u << 1;

  // ---- Shared state ----------------------------------------------------------
  unsigned g_global = 0;
  mutex g_mutex{ "g" };

  struct ctx
  {
    int id;
    unsigned local;
  };
  ctx worker_ctx[2];

  volatile unsigned g_irq_uart = 0;
  volatile unsigned g_irq_t2 = 0; // Timer0, the alarm-2 role
  volatile unsigned g_irq_t3 = 0; // Timer1, the alarm-3 role
  volatile bool g_in_uart = false;
  volatile unsigned g_t2_in_uart = 0;
  volatile unsigned g_t3_in_uart = 0;

  // ---- Handlers ----------------------------------------------------------------
  void
  uart4_handler (void)
  {
    const std::uint32_t entry = timer_us32 ();
    // Stop the TX-empty interrupt that the trigger thread started.
    reg (UART4 + U_IER) = reg (UART4 + U_IER) & ~IER_ETBEI;
    touch (UART4 + U_IIR);
    g_irq_uart = g_irq_uart + 1;

    // Keep this low-priority handler busy for 2 ms with Timer0 due after
    // 500 us: Timer0 (higher priority) must preempt it.
    g_in_uart = true;
    timer_start (0, 500);
    while ((timer_us32 () - entry) < 2000u)
      {
      }
    g_in_uart = false;
  }

  void
  timer0_handler (void) // alarm 2: one-shot
  {
    timer_stop (0);
    touch (timer (0) + T_EOI);
    g_irq_t2 = g_irq_t2 + 1;
    if (g_in_uart)
      {
        g_t2_in_uart = g_t2_in_uart + 1;
      }
  }

  void
  timer1_handler (void) // alarm 3: periodic
  {
    touch (timer (1) + T_EOI);
    g_irq_t3 = g_irq_t3 + 1;
    if (g_in_uart)
      {
        g_t3_in_uart = g_t3_in_uart + 1;
      }
  }

  // ---- Threads ---------------------------------------------------------------
  void
  line (const char* role, int id, unsigned local, unsigned global)
  {
    char b[128];
    int n = std::snprintf (
        b, sizeof (b), "[%8lu ms] %-6s #%d local=%-4u global=%u  uart=%u t2=%u t3=%u nested=%u\n",
        static_cast<unsigned long> (sysclock.now ()), role, id, local, global,
        g_irq_uart, g_irq_t2, g_irq_t3, duo_irq::nested_entries);
    g_mutex.lock ();
    uart::write (b, static_cast<std::size_t> (n));
    g_mutex.unlock ();
  }

  constexpr unsigned kIters = DUO_NESTED_ITERS;

  void*
  worker (void* arg)
  {
    auto* c = static_cast<ctx*> (arg);
    while (c->local < kIters)
      {
        c->local++;
        g_mutex.lock ();
        unsigned g = ++g_global;
        g_mutex.unlock ();
        line ("worker", c->id, c->local, g);
        sysclock.sleep_for (1000);
      }
    return nullptr;
  }

  void*
  led_thread (void*)
  {
    for (;;)
      {
        sysclock.sleep_for (3000);
        for (int i = 0; i < 3; ++i)
          {
            led::on ();
            sysclock.sleep_for (200);
            led::off ();
            sysclock.sleep_for (200);
          }
      }
    return nullptr;
  }

  void*
  trigger_thread (void*)
  {
    sysclock.sleep_for (2000);
    for (;;)
      {
        sysclock.sleep_for (1500);
        // "Pend" the UART interrupt: the TX-empty interrupt fires at once
        // (the THR is empty between console writes); the handler stops it.
        g_mutex.lock ();
        reg (UART4 + U_IER) = reg (UART4 + U_IER) | IER_ETBEI;
        g_mutex.unlock ();
      }
    return nullptr;
  }

  thread::stack::element_t worker_stacks[2][2048];
  thread::stack::element_t led_stack[1024];
  thread::stack::element_t trigger_stack[1024];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  led::init ();
  uart::write ("\n=============================================\n");
  uart::write ("  " PORT_BANNER_LONG " - micro-os-plus-iii\n");
  uart::write ("  smp-test-nested : nested PLIC interrupts\n");
  uart::write ("=============================================\n");

  g_timer_hz = measure_timer_hz ();
  {
    char b[80];
    int n = std::snprintf (b, sizeof (b), "DW timer clock (measured vs rdtime): %lu Hz\n",
                           static_cast<unsigned long> (g_timer_hz));
    uart::write (b, static_cast<std::size_t> (n));
  }

  const std::uint64_t run_r0 = riscv::csr::rdtime ();
  const std::uint32_t run_t0 = *reinterpret_cast<volatile std::uint32_t*> (0x030A0000u + 0x14u * 2 + 0x04);
  const auto run_s0 = sysclock.now ();

  duo_irq::init ();
  duo_irq::set_handler (duo_irq::TIMER0, timer0_handler);
  duo_irq::set_handler (duo_irq::TIMER0 + 1, timer1_handler);
  duo_irq::set_handler (duo_irq::UART4, uart4_handler);
  duo_irq::set_priority (duo_irq::TIMER0, 3);     // Arm 8
  duo_irq::set_priority (duo_irq::TIMER0 + 1, 2); // Arm 10
  duo_irq::set_priority (duo_irq::UART4, 1);      // Arm 12
  timer_stop (0);
  timer_stop (1);
#if DUO_NESTED_STAGE >= 2
  duo_irq::enable (duo_irq::TIMER0 + 1);
  timer_start (1, 1249500);
#endif
#if DUO_NESTED_STAGE >= 3
  duo_irq::enable (duo_irq::TIMER0);
  duo_irq::enable (duo_irq::UART4);
#endif

  thread::attributes attr = thread::initializer;
  static thread* workers[2];
  static char names[2][8];
  for (int i = 0; i < 2; ++i)
    {
      worker_ctx[i] = ctx{ i, 0 };
      attr.th_stack_address = worker_stacks[i];
      attr.th_stack_size_bytes = sizeof (worker_stacks[i]);
      std::snprintf (names[i], sizeof (names[i]), "wrk%d", i);
      workers[i] = new thread (names[i], worker, &worker_ctx[i], attr);
    }
  attr.th_stack_address = led_stack;
  attr.th_stack_size_bytes = sizeof (led_stack);
  static thread led_th{ "led", led_thread, nullptr, attr };
  attr.th_stack_address = trigger_stack;
  attr.th_stack_size_bytes = sizeof (trigger_stack);
#if DUO_NESTED_STAGE >= 3
  static thread trigger_th{ "trigger", trigger_thread, nullptr, attr };
#else
  (void)trigger_thread;
#endif

  for (int i = 0; i < 2; ++i)
    {
      workers[i]->join ();
    }

  duo_irq::disable (duo_irq::UART4);
  duo_irq::disable (duo_irq::TIMER0);
  duo_irq::disable (duo_irq::TIMER0 + 1);
  timer_stop (0);
  timer_stop (1);

  {
    const std::uint64_t r = riscv::csr::rdtime () - run_r0;
    const std::uint32_t t = run_t0 - *reinterpret_cast<volatile std::uint32_t*> (0x030A0000u + 0x14u * 2 + 0x04);
    const auto sc = sysclock.now () - run_s0;
    char b[160];
    int n = std::snprintf (b, sizeof (b),
                           "elapsed: DW timer %lu ms, rdtime %lu ms, sysclock %lu ms\n",
                           static_cast<unsigned long> (static_cast<std::uint64_t> (t) * 1000u / g_timer_hz),
                           static_cast<unsigned long> (r * 1000u / riscv_board::TIMEBASE_HZ),
                           static_cast<unsigned long> (sc));
    uart::write (b, static_cast<std::size_t> (n));
    timer_stop (2); // the measurement timer: leave the block quiet
  }

  const bool ok = worker_ctx[0].local == kIters && worker_ctx[1].local == kIters
#if DUO_NESTED_STAGE >= 2
                  && g_irq_t3 > 0
#endif
#if DUO_NESTED_STAGE >= 3
                  && g_irq_uart > 0 && g_irq_t2 > 0 && g_t2_in_uart > 0
#endif
      ;
  char b[200];
  int n = std::snprintf (
      b, sizeof (b),
      "\nRESULT: %s  (wrk0=%u wrk1=%u uart=%u t2=%u t3=%u t2_in_uart=%u "
      "t3_in_uart=%u nested=%u)\n",
      ok ? "PASS" : "FAIL", worker_ctx[0].local, worker_ctx[1].local,
      g_irq_uart, g_irq_t2, g_irq_t3, g_t2_in_uart, g_t3_in_uart,
      duo_irq::nested_entries);
  g_mutex.lock ();
  uart::write (b, static_cast<std::size_t> (n));
  g_mutex.unlock ();
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
