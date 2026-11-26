/*
 * board-basics (WCH CH32V307V-EVT-R1) -- bring-up without the kernel.
 *
 * Links the port's kernel-free runtime (micro-os-plus::riscv-bare:
 * startup.S, trap.S, src/bare/runtime.cpp) and the board, nothing else.
 *
 *   - boot from flash: reset -> _start (0x00000000) -> board early init
 *     (corecfgr, clocks, SysTick) -> C++ static constructors -> board init
 *     (USART1 115200) -> main()
 *   - clock: the RCC must show HSE ready, the PLL from HSE x18 and the PLL
 *     as SYSCLK. The frequency itself is measured against the host's
 *     wall clock (semihosting SYS_TIME, whole seconds: SysTick ticks between
 *     two second edges ~9 s apart) and REPORTED; it is not part of the
 *     verdict until it is known whether SysTick counts while the core is
 *     halted for a semihosting call. The readable UART at 115200 is the
 *     other, external, check of the clock.
 *   - LED1 (PD0, active low): blinks; its output register follows on/off.
 *   - key S2 (PA1, active low, pull-up): reads released (high) at rest;
 *     presses during a 10 s window are counted and reported.
 *
 * Verdict: RESULT line on the UART and semihosting; semihosting exit.
 */

#include <cstdint>

#include <board.hpp>
#include <ch32v307.hpp>
#include <hw_result.hpp>
#include <key.hpp>
#include <led.hpp>
#include <semihosting.hpp>
#include <uart.hpp>

namespace
{
  struct ctor_probe
  {
    volatile bool ran = false;
    ctor_probe ()
    {
      ran = true;
    }
  };
  ctor_probe g_ctor_probe;

  void
  both (const char* s)
  {
    uart::write (s);
    semihosting::write_str (s);
  }

  void
  hex32 (std::uint32_t v)
  {
    char b[11] = { '0', 'x' };
    for (int i = 7; i >= 0; --i)
      {
        const unsigned d = (v >> (i * 4)) & 0xFu;
        b[2 + 7 - i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
      }
    b[10] = '\0';
    both (b);
  }

  void
  dec (std::uint32_t v)
  {
    char b[11];
    int n = 0;
    do
      {
        b[n++] = static_cast<char> ('0' + v % 10u);
        v /= 10u;
      }
    while (v != 0u);
    char o[11];
    for (int i = 0; i < n; ++i)
      {
        o[i] = b[n - 1 - i];
      }
    o[n] = '\0';
    both (o);
  }

  void
  busy_wait_ticks (std::uint64_t ticks)
  {
    const std::uint64_t t0 = ch32::systick_now ();
    while ((ch32::systick_now () - t0) < ticks)
      {
      }
  }

  void
  busy_wait_ms (std::uint32_t ms)
  {
    busy_wait_ticks (static_cast<std::uint64_t> (ms) * (board::CPU_HZ_NOMINAL / 1000u));
  }

  // SYS_TIME (0x11): host seconds since the epoch.
  long
  host_seconds (void)
  {
    return semihosting::call (0x11, nullptr);
  }

  // Poll until the host's second changes; returns the new second.
  long
  next_second_edge (void)
  {
    const long s0 = host_seconds ();
    long s;
    do
      {
        s = host_seconds ();
      }
    while (s == s0);
    return s;
  }
} // namespace

extern "C" int
main (void)
{
  using namespace ch32;

  both ("\n=============================================\n");
  both ("  WCH CH32V307V-EVT-R1 - micro-os-plus-iii RISC-V port\n");
  both ("  board-basics : bring-up without the kernel\n");
  both ("=============================================\n");

  bool ok = true;

  both ("static ctor    : ");
  both (g_ctor_probe.ran ? "RAN\n" : "NOT RUN\n");
  ok = ok && g_ctor_probe.ran;

  // --- clock configuration -------------------------------------------------
  const std::uint32_t ctlr = reg (RCC_CTLR);
  const std::uint32_t cfgr0 = reg (RCC_CFGR0);
  both ("RCC_CTLR       : ");
  hex32 (ctlr);
  both ("\nRCC_CFGR0      : ");
  hex32 (cfgr0);
  both ("\n");
  const bool hse_rdy = (ctlr & RCC_HSERDY) != 0u;
  const bool pll_rdy = (ctlr & RCC_PLLRDY) != 0u;
  const bool pll_src_hse = (cfgr0 & RCC_PLLSRC_HSE) != 0u;
  const bool pll_x18 = ((cfgr0 >> 18) & 0xFu) == 0u;
  const bool sws_pll = (cfgr0 & RCC_SWS_MASK) == RCC_SWS_PLL;
  const bool clk_ok = board::clock_from_hse () && hse_rdy && pll_rdy
                      && pll_src_hse && pll_x18 && sws_pll;
  both ("clock          : ");
  both (clk_ok ? "HSE 8 MHz -> PLL x18 = 144 MHz (configured)\n"
               : "NOT HSE/PLL x18\n");
  ok = ok && clk_ok;

  // --- clock frequency against the host (reported) -------------------------
  both ("clock measure  : ~9 s against the host's wall clock...\n");
  const long sA = next_second_edge ();
  const std::uint64_t cA = systick_now ();
  busy_wait_ticks (static_cast<std::uint64_t> (board::CPU_HZ_NOMINAL) * 8u
                   + board::CPU_HZ_NOMINAL / 2u);
  const long sB = next_second_edge ();
  const std::uint64_t cB = systick_now ();
  const std::uint32_t secs = static_cast<std::uint32_t> (sB - sA);
  const std::uint64_t ticks = cB - cA;
  both ("  host seconds : ");
  dec (secs);
  both ("\n  SysTick      : ");
  dec (static_cast<std::uint32_t> (ticks / 1000u));
  both (" k ticks\n  HCLK         : ");
  dec (static_cast<std::uint32_t> (ticks / (secs * 1000u)));
  both (" kHz (reported, not judged)\n");

  // --- LED -------------------------------------------------------------------
  led::init ();
  led::on ();
  const bool led_on_ok = led::is_on ();
  led::off ();
  const bool led_off_ok = !led::is_on ();
  both ("LED1 (PD0)     : ");
  both ((led_on_ok && led_off_ok) ? "on/off follow the pin\n" : "PIN DOES NOT FOLLOW\n");
  ok = ok && led_on_ok && led_off_ok;

  // --- key -------------------------------------------------------------------
  key::init ();
  busy_wait_ms (5);
  const bool key_idle_high = !key::pressed ();
  both ("key S2 (PA1)   : ");
  both (key_idle_high ? "released at rest (pull-up)\n" : "READS PRESSED AT REST\n");
  ok = ok && key_idle_high;

  both ("press S2 now: 10 s window, LED blinks...\n");
  std::uint32_t presses = 0;
  bool was = false;
  for (std::uint32_t i = 0; i < 10u * 1000u / 20u; ++i)
    {
      const bool now = key::pressed ();
      if (now && !was)
        {
          ++presses;
          uart::write ("  [key] pressed\n");
        }
      was = now;
      if ((i % 10u) == 0u)
        {
          led::toggle ();
        }
      busy_wait_ms (20);
    }
  led::off ();
  both ("  presses      : ");
  dec (presses);
  both ("\n");

  both (ok ? "\nRESULT: PASS (board-basics)\n" : "\nRESULT: FAIL (board-basics)\n");
  uart::flush ();
  if (ok)
    {
      hw_result::ok ();
    }
  hw_result::fail ();
  for (;;)
    {
    }
  return 0;
}
