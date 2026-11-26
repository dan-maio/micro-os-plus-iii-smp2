/*
 * RISC-V (Hazard3) port of the predecessor's smp-test-xip-core1park
 * (micro-os-plus-iii-pico2, Cortex-M): flash writes while the OTHER core is
 * busy executing from XIP flash -- the flash_safe_execute pattern.
 *
 * Turning XIP off would make core 1 fetch from a flash that is not there, so
 * core 0 first parks it in SRAM (mu_flash_core1_lockout, MIE off), waits
 * for the ack, erases/programs, then releases it:
 *
 *   core 0                                core 1 worker (busy from XIP)
 *   release = 0; req = 1  ------------->  sees req -> mu_flash_core1_lockout:
 *   wait ack                                MIE off; ack = 1; spin on release
 *   [XIP off] erase/program [XIP on]        (only SRAM here)
 *   req = 0; release = 1  ------------->  ack = 0; MIE on; back to the loop
 *   wait !ack
 *
 * Four rounds (erase a FLASH_FS sector, program a page unique to the round);
 * each must read back, and core 1's counter (incremented by code running
 * from flash) must have moved across the round.
 */

#include <cmsis-plus/rtos/os.h>

#include <flash_rp2350.h>
#include <led.hpp>
#include <multicore.hpp>
#include <uart.hpp>

#include <hw_result.hpp>
#include <riscv-smp.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace os::rtos;

extern "C"
{
  extern char _flash_fs_begin; // 0x10100000 (linker-flatfs.ld)
}

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
  // Cross-core hand-off flags, in SRAM (.bss on linker-flatfs.ld).
  volatile std::uint32_t g_park_req = 0;
  volatile std::uint32_t g_park_ack = 0;
  volatile std::uint32_t g_release = 0;
  volatile std::uint32_t g_core1_counter = 0; // core 1 running from XIP
  volatile std::uint32_t g_core1_running = 0;

  inline void
  fence (void)
  {
    __asm__ volatile("fence rw, rw" ::: "memory");
  }

  void
  hex (const char* k, unsigned long v)
  {
    char b[16];
    int n = std::snprintf (b, sizeof (b), "0x%08lx", v);
    uart::write (k);
    uart::write (b, static_cast<std::size_t> (n));
    uart::write ("\n");
  }

  void*
  secondary_idle_func (void*)
  {
    this_thread::thread ().priority (thread::priority::idle);
    for (;;)
      {
        this_thread::yield ();
      }
    return nullptr;
  }

  // Core 1: never blocks; counts from flash and parks on request.
  void*
  core1_worker (void*)
  {
    g_core1_running = 1;
    for (;;)
      {
        if (g_park_req)
          {
            mu_flash_core1_lockout (&g_park_ack, &g_release);
          }
        g_core1_counter = g_core1_counter + 1;
        fence ();
      }
    return nullptr;
  }

  thread::stack::element_t idle1_stack[512];
  thread::stack::element_t worker_stack[1024];
  std::uint8_t g_page[MU_FLASH_PAGE];

  // Park core 1, run fn, release core 1.
  template <class F>
  void
  flash_safe (F&& fn)
  {
    g_release = 0;
    fence ();
    g_park_req = 1;
    fence ();
    while (!g_park_ack)
      {
        fence ();
      }
    fn ();
    g_park_req = 0;
    fence ();
    g_release = 1;
    fence ();
    while (g_park_ack)
      {
        fence ();
      }
  }
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  this_thread::thread ().cpu_affinity (1u << 0);
  led::init ();

  uart::write ("\n=============================================\n");
  uart::write ("  WeAct RP2350B (Hazard3) - micro-os-plus-iii\n");
  uart::write ("  smp-test-xip-core1park : flash_safe_execute\n");
  uart::write ("=============================================\n");

  const std::uint32_t fs_addr = static_cast<std::uint32_t> (
      reinterpret_cast<std::uintptr_t> (&_flash_fs_begin));
  const std::uint32_t fs_off = fs_addr - MU_FLASH_XIP_BASE;
  hex ("FLASH_FS @ ", fs_addr);

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = idle1_stack;
  attr.th_stack_size_bytes = sizeof (idle1_stack);
  static thread idle1{ "idle1", secondary_idle_func, nullptr, attr };
  os::rtos::scheduler::os_idle_thread_core[1] = &idle1;

  uart::write ("core 0: launching core 1...\n");
  multicore::launch_core1 (port_smp_secondary_start, port_core1_stack_top ());
  port_smp_core0_ipi_enable ();

  attr.th_stack_address = worker_stack;
  attr.th_stack_size_bytes = sizeof (worker_stack);
  static thread worker{ "worker", core1_worker, nullptr, attr };
  worker.cpu_affinity (1u << 1);

  while (!g_core1_running)
    {
      fence ();
    }
  sysclock.sleep_for (50);
  std::uint32_t c_prev = g_core1_counter;
  {
    char b[64];
    int n = std::snprintf (b, sizeof (b), "core 1 busy from XIP, counter=%lu\n",
                           static_cast<unsigned long> (c_prev));
    uart::write (b, static_cast<std::size_t> (n));
  }

  unsigned pass = 0, fail = 0;
  constexpr unsigned ROUNDS = 4;
  for (unsigned r = 0; r < ROUNDS; ++r)
    {
      std::memset (g_page, 0, sizeof (g_page));
      std::snprintf (reinterpret_cast<char*> (g_page), 16, "XIP-R%u", r);
      for (unsigned i = 16; i < MU_FLASH_PAGE; ++i)
        {
          g_page[i] = static_cast<std::uint8_t> (i + r);
        }

      flash_safe ([&] { mu_flash_erase (fs_off, MU_FLASH_SECTOR); });
      mu_flash_xip_flush ();
      const bool erased
          = (*reinterpret_cast<const volatile std::uint8_t*> (fs_addr) == 0xFF);

      flash_safe ([&] { mu_flash_program (fs_off, g_page, MU_FLASH_PAGE); });
      mu_flash_xip_flush ();
      const bool progd
          = (std::memcmp (reinterpret_cast<const void*> (fs_addr), g_page,
                          MU_FLASH_PAGE)
             == 0);

      const std::uint32_t c_now = g_core1_counter;
      const bool alive = (c_now != c_prev);
      c_prev = c_now;

      const bool ok = erased && progd && alive;
      ok ? ++pass : ++fail;
      char b[96];
      int m = std::snprintf (b, sizeof (b),
                             "round %u: erase=%s prog=%s core1_alive=%s "
                             "(cnt=%lu)\n",
                             r, erased ? "OK" : "FAIL", progd ? "OK" : "FAIL",
                             alive ? "yes" : "NO",
                             static_cast<unsigned long> (c_now));
      uart::write (b, static_cast<std::size_t> (m));
      led::toggle ();
    }

  char rb[64];
  int n = std::snprintf (rb, sizeof (rb),
                         "\nRESULT: %s  (%u passed, %u failed)\n",
                         fail == 0 ? "PASS" : "FAIL", pass, fail);
  uart::write ("---------------------------------------------\n");
  uart::write (rb, static_cast<std::size_t> (n));
  if (fail == 0)
    {
      hw_result::ok ();
    }
  else
    {
      hw_result::fail ();
    }
  return 0;
}
