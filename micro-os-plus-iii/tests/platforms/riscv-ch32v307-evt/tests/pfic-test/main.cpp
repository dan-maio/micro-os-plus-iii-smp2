/*
 * pfic-test (WCH CH32V307V-EVT-R1) -- the port's PFIC path under the kernel.
 *
 * Two peripheral interrupts nobody uses here, raised by software through
 * PFIC_IPSR (their peripherals stay off):
 *   IRQ 16 (WWDG) at 0x40  kernel-aware: may call the kernel
 *   IRQ 17 (PVD)  at 0x00  zero-latency: never masked, must not call it
 *
 *   1 wake       IRQ 16 posts a semaphore a thread waits on
 *   2 preempt    a higher-priority thread woken by IRQ 16 runs BEFORE the
 *                lower thread that raised IRQ 16 continues (a), and one
 *                woken by a thread inside a critical section runs when the
 *                section ends, before that thread goes on (b)
 *   3 nesting    inside IRQ 16, raising IRQ 17 preempts it at once:
 *                nesting 2, PFIC_GISR NESTSTA = 0x03
 *   4 BASEPRI    inside a critical section IRQ 17 runs at once, IRQ 16 is
 *                held until the section ends (and then runs)
 *   5 tick       a kernel timer callback runs in the SysTick handler
 *                (IACTR0 bit 12 set): IRQ 16 raised there preempts it
 *   6 stress     2000 IRQ 16 -> thread hand-offs with SysTick running,
 *                none lost
 *
 * RESULT on the UART and semihosting; semihosting exit.
 */

#include <cmsis-plus/rtos/os.h>

#include <cstdint>
#include <cstdio>

#include <hw_result.hpp>
#include <pfic.hpp>
#include <semihosting.hpp>
#include <uart.hpp>

using namespace os::rtos;

extern "C" volatile std::uint32_t riscv_trap_nesting[];

namespace
{
  constexpr unsigned kIrqKernel = 16; // WWDG, 0x40
  constexpr unsigned kIrqZero = 17;   // PVD, 0x00

  void
  both (const char* s)
  {
    uart::write (s);
    semihosting::write_str (s);
  }

  void
  say (const char* fmt, unsigned a = 0, unsigned b = 0, unsigned c = 0)
  {
    char buf[128];
    std::snprintf (buf, sizeof (buf), fmt, a, b, c);
    both (buf);
  }

  // What IRQ 16 does this time.
  enum class mode : unsigned
  {
    post,       // post g_sem
    post_high,  // post g_high_go (test 2)
    nest,       // raise IRQ 17 and wait for it
  };
  volatile mode g_mode = mode::post;

  semaphore_counting g_sem{ "sem", 10000, 0 };
  semaphore_binary g_high_go{ "high_go", 0 };

  volatile std::uint32_t g_irq16_count = 0;
  volatile std::uint32_t g_irq17_count = 0;
  volatile std::uint32_t g_irq17_nesting = 0;
  volatile std::uint32_t g_irq17_gisr = 0;
  volatile bool g_irq17_done = false;
  volatile bool g_irq16_saw_17 = false;
  volatile bool g_irq16_systick_active = false;

  // Order of events (test 2).
  volatile std::uint32_t g_seq = 0;
  volatile std::uint32_t g_high_seq = 0;
  volatile std::uint32_t g_low_after_seq = 0;

  std::uint32_t
  next_seq (void)
  {
    const std::uint32_t v = g_seq + 1u;
    g_seq = v;
    return v;
  }

  void
  raise (unsigned irq)
  {
    pfic::set_pending (irq);
    __asm__ volatile("fence.i" ::: "memory");
  }

  // Give a raised interrupt time to be taken (a few instructions).
  void
  settle (void)
  {
    for (int i = 0; i < 100; ++i)
      {
        __asm__ volatile("nop");
      }
  }
} // namespace

// The board dispatches PFIC interrupts to WCH's handler names
// (test/boards/ch32v307-evt/src/irq.cpp): 16 = WWDG, 17 = PVD.
extern "C" void
PVD_IRQHandler (void)
{
  // Zero-latency: no kernel calls.
  g_irq17_nesting = riscv_trap_nesting[0];
  g_irq17_gisr = pfic::reg32 (pfic::GISR);
  g_irq17_count = g_irq17_count + 1;
  g_irq17_done = true;
}

extern "C" void
WWDG_IRQHandler (void)
{
  g_irq16_count = g_irq16_count + 1;
  g_irq16_systick_active
      = (pfic::reg32 (pfic::IACTR0) >> pfic::IRQ_SYSTICK) & 1u;
  if (g_mode == mode::nest)
    {
      g_irq17_done = false;
      raise (kIrqZero);
      // Taken a few instructions later, inside this handler.
      for (int i = 0; i < 1000 && !g_irq17_done; ++i)
        {
        }
      g_irq16_saw_17 = g_irq17_done;
    }
  else if (g_mode == mode::post_high)
    {
      g_high_go.post ();
    }
  else
    {
      g_sem.post ();
    }
}

namespace
{
  // ---- test 2: the high thread --------------------------------------------
  thread::stack::element_t high_stack[512];

  void*
  high_thread (void*)
  {
    for (;;)
      {
        g_high_go.wait ();
        g_high_seq = next_seq ();
      }
    return nullptr;
  }

  // ---- test 5: the timer callback ------------------------------------------
  volatile bool g_cb_ran = false;
  volatile bool g_cb_in_systick = false;
  volatile bool g_cb_irq16_inside = false;

  void
  timer_cb (void*)
  {
    g_cb_in_systick = (pfic::reg32 (pfic::IACTR0) >> pfic::IRQ_SYSTICK) & 1u;
    g_mode = mode::nest; // IRQ 16 then does not touch the kernel
    const std::uint32_t before = g_irq16_count;
    raise (kIrqKernel);
    for (int i = 0; i < 1000 && g_irq16_count == before; ++i)
      {
      }
    g_cb_irq16_inside = (g_irq16_count != before) && g_irq16_systick_active;
    g_cb_ran = true;
  }

  timer g_timer{ "tmr", timer_cb, nullptr };

  // ---- test 6: the waiter --------------------------------------------------
  volatile std::uint32_t g_woken = 0;
  thread::stack::element_t waiter_stack[512];

  void*
  waiter_thread (void*)
  {
    for (;;)
      {
        if (g_sem.timed_wait (1000) != result::ok)
          {
            continue;
          }
        g_woken = g_woken + 1;
      }
    return nullptr;
  }
} // namespace

int
os_main (int, char*[])
{
  both ("\n=============================================\n");
  both ("  WCH CH32V307V-EVT-R1 - micro-os-plus-iii\n");
  both ("  pfic-test : the PFIC path under the kernel\n");
  both ("=============================================\n");

  pfic::set_priority (kIrqKernel, pfic::PRIO_KERNEL_HIGH);
  pfic::set_priority (kIrqZero, pfic::PRIO_ZERO_LATENCY);
  pfic::clear_pending (kIrqKernel);
  pfic::clear_pending (kIrqZero);
  pfic::enable (kIrqKernel);
  pfic::enable (kIrqZero);

  bool all = true;

  // ---- 1 wake ---------------------------------------------------------------
  {
    g_mode = mode::post;
    raise (kIrqKernel);
    const bool ok = g_sem.timed_wait (100) == result::ok;
    both (ok ? "1 wake      : IRQ 16 posted, the thread woke\n              PASS\n"
             : "1 wake      : IRQ 16 posted, the thread did NOT wake\n              FAIL\n");
    all = all && ok;
  }

  // ---- 2 preempt ------------------------------------------------------------
  {
    thread::attributes attr;
    attr.th_priority = thread::priority::high;
    attr.th_stack_address = high_stack;
    attr.th_stack_size_bytes = sizeof (high_stack);
    static thread high{ "high", high_thread, nullptr, attr };
    sysclock.sleep_for (2); // let it reach its wait

    // (a) from an interrupt: wait until IRQ 16 has run, then take a number.
    g_mode = mode::post_high;
    g_seq = 0;
    g_high_seq = 0;
    const std::uint32_t c16 = g_irq16_count;
    raise (kIrqKernel);
    while (g_irq16_count == c16)
      {
      }
    g_low_after_seq = next_seq ();
    const bool ok_a = (g_high_seq == 1u) && (g_low_after_seq == 2u);
    say ("2a preempt  : from IRQ 16: high ran at %u, low continued at %u\n",
         g_high_seq, g_low_after_seq);

    // (b) from a thread, inside a critical section.
    g_seq = 0;
    g_high_seq = 0;
    {
      interrupts::critical_section ics;
      g_high_go.post ();
      g_seq = 10; // still this thread: high must not have run yet
    }
    g_low_after_seq = next_seq ();
    const bool ok_b = (g_high_seq == 11u) && (g_low_after_seq == 12u);
    say ("2b preempt  : in a section: high ran at %u, low continued at %u\n",
         g_high_seq, g_low_after_seq);
    const bool ok = ok_a && ok_b;
    both (ok ? "              PASS\n" : "              FAIL\n");
    all = all && ok;
  }

  // ---- 3 nesting ------------------------------------------------------------
  {
    g_mode = mode::nest;
    g_irq16_saw_17 = false;
    raise (kIrqKernel);
    settle ();
    settle ();
    const bool ok = g_irq16_saw_17 && g_irq17_nesting == 2u
                    && (g_irq17_gisr & 0xFFu) == 0x03u;
    say ("3 nesting   : IRQ 17 inside IRQ 16: %u, nesting %u, GISR 0x%x\n",
         g_irq16_saw_17 ? 1u : 0u, g_irq17_nesting, g_irq17_gisr);
    both (ok ? "              PASS\n" : "              FAIL\n");
    all = all && ok;
  }

  // ---- 4 BASEPRI ------------------------------------------------------------
  {
    g_mode = mode::nest; // IRQ 16 then raises 17 itself; count both
    const std::uint32_t c16 = g_irq16_count;
    const std::uint32_t c17 = g_irq17_count;
    std::uint32_t in16;
    std::uint32_t in17;
    std::uint32_t thr;
    {
      interrupts::critical_section ics;
      thr = pfic::threshold ();
      raise (kIrqKernel);
      raise (kIrqZero);
      settle ();
      in16 = g_irq16_count - c16;
      in17 = g_irq17_count - c17;
    }
    settle ();
    const std::uint32_t after16 = g_irq16_count - c16;
    const bool ok = (thr == pfic::KERNEL_THRESHOLD) && in16 == 0u && in17 == 1u
                    && after16 == 1u;
    say ("4 BASEPRI   : threshold 0x%x; inside: IRQ16 %u, IRQ17 %u;", thr, in16,
         in17);
    say (" after: IRQ16 %u\n", after16);
    both (ok ? "              PASS\n" : "              FAIL\n");
    all = all && ok;
  }

  // ---- 5 tick ---------------------------------------------------------------
  {
    g_cb_ran = false;
    g_timer.start (5);
    sysclock.sleep_for (20);
    const bool ok = g_cb_ran && g_cb_in_systick && g_cb_irq16_inside;
    say ("5 tick      : callback ran %u, in SysTick %u, IRQ 16 inside it %u\n",
         g_cb_ran ? 1u : 0u, g_cb_in_systick ? 1u : 0u,
         g_cb_irq16_inside ? 1u : 0u);
    both (ok ? "              PASS\n" : "              FAIL\n");
    all = all && ok;
  }

  // ---- 6 stress -------------------------------------------------------------
  {
    g_mode = mode::post;
    while (g_sem.try_wait () == result::ok)
      {
      }
    thread::attributes attr;
    attr.th_priority = thread::priority::above_normal;
    attr.th_stack_address = waiter_stack;
    attr.th_stack_size_bytes = sizeof (waiter_stack);
    static thread waiter{ "waiter", waiter_thread, nullptr, attr };
    sysclock.sleep_for (2);

    constexpr std::uint32_t kN = 2000;
    const std::uint32_t w0 = g_woken;
    const std::uint32_t t0 = sysclock.now ();
    for (std::uint32_t i = 0; i < kN; ++i)
      {
        raise (kIrqKernel);
        // The waiter (higher priority) runs before this loop goes on.
      }
    sysclock.sleep_for (20);
    const std::uint32_t got = g_woken - w0;
    const std::uint32_t ms = sysclock.now () - t0;
    const bool ok = got == kN;
    say ("6 stress    : %u raised, %u woke the thread, %u ms\n", kN, got, ms);
    both (ok ? "              PASS\n" : "              FAIL\n");
    all = all && ok;
  }

  both (all ? "\nRESULT: PASS (pfic-test)\n" : "\nRESULT: FAIL (pfic-test)\n");
  uart::flush ();
  if (all)
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
