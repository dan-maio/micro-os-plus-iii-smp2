/*
 * RISC-V (Hazard3) port of the predecessor's smp-test-xip-fs
 * (micro-os-plus-iii-pico2, Cortex-M): the flat file system on real flash.
 *
 *   1. the append-log flat-fs (fs-archive.cpp, the predecessor's, unchanged)
 *      on the board's flash writer (flash-prog-pico2.cpp -> mu_flash_*),
 *   2. format FLASH_FS (0x10100000, linker-flatfs.ld) and add three files,
 *   3. list them; each file's CRC-32 recomputed through XIP reads, its bytes
 *      compared with the source, and found again by name.
 *
 * Not here: the predecessor's step 3, the XIP classifier
 * (xip::compact_for_xip). It parses the stored .so files as loadable
 * modules, and the fixtures are ARM ELF objects; it belongs to the loader
 * stage, with RISC-V fixtures. The same three files are kept as payloads:
 * to the file system they are bytes.
 *
 * Flash writes run before core 1 is launched (a write turns XIP off); then
 * core 1 runs a thread, and the run ends in the semihosting exit.
 */

#include <cmsis-plus/rtos/os.h>

#include <led.hpp>
#include <multicore.hpp>
#include <uart.hpp>

#include <hw_result.hpp>
#include <riscv-smp.hpp>

#include "fs-archive.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "fixtures.inc"

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
  void
  say (const char* fmt, ...)
  {
    char b[160];
    va_list ap;
    va_start (ap, fmt);
    int n = std::vsnprintf (b, sizeof (b), fmt, ap);
    va_end (ap);
    if (n > static_cast<int> (sizeof (b) - 1))
      {
        n = sizeof (b) - 1;
      }
    if (n > 0)
      {
        uart::write (b, static_cast<std::size_t> (n));
      }
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
} // namespace

int
os_main (int /*argc*/, char* /*argv*/[])
{
  this_thread::thread ().cpu_affinity (1u << 0);
  led::init ();

  say ("\n=============================================\n");
  say ("  WeAct RP2350B (Hazard3) - micro-os-plus-iii\n");
  say ("  smp-test-xip-fs : flat-fs on flash\n");
  say ("=============================================\n");

  unsigned fail = 0;

  // --- provision (core 1 not launched yet) ---
  say ("[fs] format FLASH_FS...\n");
  if (!fw::fs_format ())
    {
      say ("     fs_format FAILED\n");
      ++fail;
    }
  if (!fw::fs_valid ())
    {
      say ("     superblock not valid after format\n");
      ++fail;
    }

  for (std::size_t i = 0; i < g_fixtures_count; ++i)
    {
      const auto& fx = g_fixtures[i];
      auto r = fw::fs_add (fx.name, fx.data, fx.len);
      say ("[fs] add %-10s %5u B  %s\n", fx.name, static_cast<unsigned> (fx.len),
           r ? "ok" : "FAIL");
      if (!r)
        {
          ++fail;
        }
    }

  // --- list; CRC, content and lookup through XIP ---
  say ("\n[fs] %u files, %u bytes used of %u\n",
       static_cast<unsigned> (fw::fs_count ()),
       static_cast<unsigned> (fw::fs_used ()), 3u * 1024u * 1024u);
  if (fw::fs_count () != g_fixtures_count)
    {
      ++fail;
    }
  for (std::uint32_t i = 0; i < fw::fs_count (); ++i)
    {
      const fw::FsEntry* e = fw::fs_entry (i);
      if (!e)
        {
          ++fail;
          break;
        }
      const std::uint32_t crc = fw::crc32_calc (fw::fs_blob (*e), e->size);
      const bool crc_ok = (crc == e->crc32);
      const bool same = (i < g_fixtures_count)
                        && e->size == g_fixtures[i].len
                        && std::memcmp (fw::fs_blob (*e), g_fixtures[i].data,
                                        e->size)
                               == 0;
      const fw::FsEntry* f = fw::fs_find (e->name);
      const bool found = (f == e);
      say ("   %-10s off=0x%06x size=%5u crc=%08x %s bytes=%s find=%s\n",
           e->name, static_cast<unsigned> (e->off),
           static_cast<unsigned> (e->size), static_cast<unsigned> (e->crc32),
           crc_ok ? "OK" : "BAD", same ? "OK" : "BAD", found ? "OK" : "BAD");
      if (!crc_ok || !same || !found)
        {
          ++fail;
        }
    }
  if (fw::fs_find ("missing.so") != nullptr)
    {
      say ("   fs_find(\"missing.so\") found something\n");
      ++fail;
    }

  // --- SMP after the flash work ---
  thread::attributes attr = thread::initializer;
  attr.th_stack_address = idle1_stack;
  attr.th_stack_size_bytes = sizeof (idle1_stack);
  static thread idle1{ "idle1", secondary_idle_func, nullptr, attr };
  os::rtos::scheduler::os_idle_thread_core[1] = &idle1;
  attr.th_stack_address = beat_stack;
  attr.th_stack_size_bytes = sizeof (beat_stack);
  static thread beat{ "beat", core1_beat, nullptr, attr };
  beat.cpu_affinity (1u << 1);

  say ("core 0: launching core 1...\n");
  multicore::launch_core1 (port_smp_secondary_start, port_core1_stack_top ());
  port_smp_core0_ipi_enable ();
  for (int i = 0; i < 20 && g_core1_beats < 10; ++i)
    {
      led::toggle ();
      sysclock.sleep_for (100);
    }
  say ("core 1 beats = %u\n", g_core1_beats);
  if (g_core1_beats != 10)
    {
      ++fail;
    }

  say ("\n---------------------------------------------\n");
  say ("RESULT: %s  (%u failures)\n", fail == 0 ? "PASS" : "FAIL", fail);
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
