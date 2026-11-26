/*
 * smp-test0 (Pico 2 / RP2350, Hazard3) -- bare dual-core bring-up, no RTOS.
 *
 * The RISC-V counterpart of the Cortex-M Pico 2 smp-test0 (micro-os-plus-
 * iii-cortexm, test/pico2/smp-test0): the same checks, before any kernel.
 * It links the port's kernel-free runtime (micro-os-plus::riscv-bare:
 * startup.S, trap.S, src/bare/runtime.cpp) and the board, nothing else.
 *
 *   - boot: the boot ROM's IMAGE_DEF switch to Hazard3 -> _start -> board
 *     early init (clocks 150 MHz) -> C++ static constructors -> board init
 *     (UART0 115200) -> main()
 *   - a global object's constructor runs (proves .init_array)
 *   - core 0 prints its core id and a tick counter on the UART
 *   - core 1 is launched through the boot ROM FIFO handshake (the board's
 *     multicore::launch_core1) and blinks the LED, independently
 *
 * Verdict: core 1 marked itself alive and looped. The RESULT line goes to
 * the UART and (the board copies verdict lines) to semihosting; the run ends
 * with the semihosting exit.
 */

#include <cstdint>

#include <hw_result.hpp>
#include <led.hpp>
#include <multicore.hpp>
#include <rp2350.hpp>
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

  // The SIO platform timer counts microseconds (board.cpp: TICKS 1 us).
  void
  busy_wait_ms (std::uint32_t ms)
  {
    const std::uint32_t t0 = rp2350::reg (rp2350::SIO_MTIME);
    while ((rp2350::reg (rp2350::SIO_MTIME) - t0) < ms * 1000u)
      {
      }
  }

  void
  hex32 (std::uint32_t v)
  {
    char b[9];
    for (int i = 7; i >= 0; --i)
      {
        const unsigned d = (v >> (i * 4)) & 0xFu;
        b[7 - i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
      }
    b[8] = '\0';
    uart::write (b);
  }

  alignas (16) std::uint32_t core1_stack[512];

  volatile std::uint32_t g_core1_alive = 0;
  volatile std::uint32_t g_core1_loops = 0;
  volatile std::uint32_t g_core1_id = 0xFFu;

  // Core 1: blink the LED. No UART, no talk to core 0 beyond the flags.
  void
  core1_entry (void)
  {
    g_core1_alive = 0xC0FFEEu; // FIRST thing: prove it ran
    g_core1_id = rp2350::cpu_id ();
    led::init ();
    for (;;)
      {
        led::on ();
        busy_wait_ms (300);
        led::off ();
        busy_wait_ms (300);
        g_core1_loops = g_core1_loops + 1;
      }
  }
} // namespace

extern "C" int
main (void)
{
  uart::write ("\n");
  uart::write ("=============================================\n");
  uart::write ("  Pico 2 / RP2350 (Hazard3) - micro-os-plus-iii port\n");
  uart::write ("  smp-test0 : bare dual-core bring-up\n");
  uart::write ("=============================================\n");

  uart::write ("core id        : ");
  uart::put_char (static_cast<char> ('0' + rp2350::cpu_id ()));
  uart::write ("\nstatic ctor    : ");
  uart::write (g_ctor_probe.ran ? "RAN" : "NOT RUN");
  uart::write ("\nsystem clock   : 150 MHz\n");
  uart::write ("UART0          : 115200 8N1 (GPIO0=TX, GPIO1=RX)\n");

  uart::write ("\nlaunching core 1 (LED blink)...\n");
  multicore::launch_core1 (
      core1_entry, reinterpret_cast<std::uintptr_t> (core1_stack)
                       + sizeof (core1_stack));
  uart::write ("core 1 launch returned.\n\n");

  constexpr unsigned kIterations = 20;
  for (unsigned tick = 0; tick < kIterations; ++tick)
    {
      uart::write ("[core0] tick 0x");
      hex32 (tick);
      uart::write ("  core1_alive=0x");
      hex32 (g_core1_alive);
      uart::write ("  core1_loops=0x");
      hex32 (g_core1_loops);
      uart::write ("\n");
      busy_wait_ms (500);
    }

  const bool ok = (g_core1_alive == 0xC0FFEEu) && (g_core1_loops != 0u)
                  && (g_core1_id == 1u) && g_ctor_probe.ran;
  uart::write ("\ncore 1 launch  : ");
  uart::write (ok ? "PASS" : "FAIL");
  uart::write (ok ? "\nRESULT: PASS (smp-test0)\n" : "\nRESULT: FAIL (smp-test0)\n");
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
