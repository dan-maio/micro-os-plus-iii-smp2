/*
 * RISC-V (Hazard3) port of the predecessor's smp-test-xip
 * (micro-os-plus-iii-pico2, Cortex-M): the board proof of the RAM-resident
 * QSPI flash writer (test/boards/pico2/src/flash_rp2350.cpp) and of the
 * flat-fs layout (linker-flatfs.ld: image in the first 1 MiB, FLASH_FS at
 * 0x10100000).
 *
 *   T1  erase the first 4 KiB sector of FLASH_FS; it reads back 0xFF
 *   T2  program a 256 B page (marker + pattern); it reads back through XIP
 *
 * The flash work runs before core 1 is launched (XIP is off during it); then
 * core 1 starts and both cores run threads for 2 s, to show the kernel is
 * healthy after the writes. Ends in the semihosting exit (the predecessor
 * blinked forever).
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

  volatile unsigned g_core1_beats = 0;

  void*
  core1_beat (void*)
  {
    for (int i = 0; i < 10; ++i)
      {
        g_core1_beats = g_core1_beats + 1;
        sysclock.sleep_for (100);
      }
    return nullptr;
  }

  thread::stack::element_t idle1_stack[512];
  thread::stack::element_t beat_stack[512];

  // The page to program: in RAM, as mu_flash_program needs.
  std::uint8_t g_page[MU_FLASH_PAGE];
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  this_thread::thread ().cpu_affinity (1u << 0);
  led::init ();

  uart::write ("\n=============================================\n");
  uart::write ("  WeAct RP2350B (Hazard3) - micro-os-plus-iii\n");
  uart::write ("  smp-test-xip : flash writer + FLASH_FS proof\n");
  uart::write ("=============================================\n");

  const std::uint32_t fs_addr = static_cast<std::uint32_t> (
      reinterpret_cast<std::uintptr_t> (&_flash_fs_begin));
  const std::uint32_t fs_off = fs_addr - MU_FLASH_XIP_BASE;
  const volatile std::uint8_t* xip
      = reinterpret_cast<const volatile std::uint8_t*> (fs_addr);
  hex ("FLASH_FS @ ", fs_addr);
  hex ("  offset  ", fs_off);

  const char* marker = "XIP-FLASH-OK";
  std::memset (g_page, 0, sizeof (g_page));
  std::memcpy (g_page, marker, std::strlen (marker));
  for (unsigned i = 16; i < MU_FLASH_PAGE; ++i)
    {
      g_page[i] = static_cast<std::uint8_t> (i ^ 0x5A);
    }

  unsigned pass = 0, fail = 0;

  uart::write ("T1: erase 4K sector...\n");
  mu_flash_erase (fs_off, MU_FLASH_SECTOR);
  mu_flash_xip_flush ();
  const bool erased = (xip[0] == 0xFF && xip[128] == 0xFF
                       && xip[MU_FLASH_SECTOR - 1] == 0xFF);
  erased ? ++pass : ++fail;
  uart::write (erased ? "    erased -> 0xFF : PASS\n"
                      : "    erase check    : FAIL\n");
  uart::write (mu_flash_xip_via_stub ? "    XIP re-entered via the boot ROM stub\n"
                                     : "    XIP re-entered via enter_cmd_xip\n");

  uart::write ("T2: program 256B page...\n");
  mu_flash_program (fs_off, g_page, MU_FLASH_PAGE);
  mu_flash_xip_flush ();
  const bool progd
      = (std::memcmp (reinterpret_cast<const void*> (fs_addr), g_page,
                      MU_FLASH_PAGE)
         == 0);
  progd ? ++pass : ++fail;
  uart::write (progd ? "    read-back match : PASS\n"
                     : "    read-back match : FAIL\n");
  {
    char b[40];
    int n = std::snprintf (b, sizeof (b), "    xip[0..11] = \"%.12s\"\n",
                           reinterpret_cast<const char*> (fs_addr));
    uart::write (b, static_cast<std::size_t> (n));
  }

  // SMP after the flash work: core 1 runs a thread for ~1 s.
  thread::attributes attr = thread::initializer;
  attr.th_stack_address = idle1_stack;
  attr.th_stack_size_bytes = sizeof (idle1_stack);
  static thread idle1{ "idle1", secondary_idle_func, nullptr, attr };
  os::rtos::scheduler::os_idle_thread_core[1] = &idle1;

  attr.th_stack_address = beat_stack;
  attr.th_stack_size_bytes = sizeof (beat_stack);
  static thread beat{ "beat", core1_beat, nullptr, attr };
  beat.cpu_affinity (1u << 1);

  uart::write ("core 0: launching core 1 (post-flash SMP liveness)...\n");
  multicore::launch_core1 (port_smp_secondary_start, port_core1_stack_top ());
  port_smp_core0_ipi_enable ();
  for (int i = 0; i < 20 && g_core1_beats < 10; ++i)
    {
      led::toggle ();
      sysclock.sleep_for (100);
    }
  const bool smp = (g_core1_beats == 10);
  smp ? ++pass : ++fail;
  {
    char b[64];
    int n = std::snprintf (b, sizeof (b), "T3: core 1 beats = %u : %s\n",
                           g_core1_beats, smp ? "PASS" : "FAIL");
    uart::write (b, static_cast<std::size_t> (n));
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
