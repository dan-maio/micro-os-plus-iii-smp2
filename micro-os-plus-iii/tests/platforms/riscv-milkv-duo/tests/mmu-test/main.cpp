/*
 * mmu-test -- the Sv39 page tables translate, proven from machine mode.
 *
 * The cv1800 Eclipse project enabled Sv39 and then made its accesses in
 * M-mode, where satp does not apply: the tables were built but never used.
 * This test makes them carry real accesses. With mstatus.MPRV=1 and MPP=S an
 * M-mode load/store is translated through satp exactly as an S-mode one
 * (src/mmu.cpp, load_translated / store_translated).
 *
 *   1. build the identity map of the board's regions (riscv-board.hpp)
 *   2. the software walk agrees: every region maps to itself
 *   3. identity: a word stored through the MMU at a physical address reads
 *      back physically, and vice versa
 *   4. NOT identity: map VA 0x4000_0000 -> the board's scratch DDR, store
 *      through the alias, read the physical page -- and the reverse. A
 *      translation that silently did nothing would pass 3 and fail 4.
 *   5. misaligned 64-bit accesses (the C906 does them in hardware)
 *   6. the scheduler still runs (the tick and the context switch use no
 *      translation; MPRV is cleared again after every access)
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include <board.hpp>
#include <hw_result.hpp>
#include <mmu.hpp>
#include <riscv-board.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

using namespace os::rtos;

namespace
{
  bool all_ok = true;

  void
  report (const char* what, bool ok)
  {
    char b[120];
    std::snprintf (b, sizeof (b), "  %-52s %s\n", what, ok ? "ok" : "FAIL");
    uart::write (b);
    all_ok &= ok;
  }

  volatile std::uint64_t&
  phys (std::uint64_t pa)
  {
    return *reinterpret_cast<volatile std::uint64_t*> (pa);
  }
} // namespace

#if __riscv_xlen != 64

int
os_main (int, char*[])
{
  uart::init (board::UART_BAUD);
  uart::write ("\nmmu-test on " PORT_BANNER_LONG "\n");
  uart::write ("RESULT: SKIP (mmu-test: Sv39 needs RV64; no MMU on RV32)\n");
  hw_result::ok ();
  return 0;
}

#else

int
os_main (int, char*[])
{
  uart::init (board::UART_BAUD);
  uart::write ("\nmmu-test on " PORT_BANNER_LONG "\n");

  // 1.
  mmu::pmp_allow_all ();
  report ("Sv39 tables built for the board's regions", mmu::init ());

  char b[96];
  std::snprintf (b, sizeof (b), "  satp = 0x%016" PRIx64 "\n", mmu::satp ());
  uart::write (b);

  // 2.
  {
    bool ok = true;
    for (const mmu::region& r : riscv_board::MMU_REGIONS)
      {
        ok &= mmu::translate (r.base) == r.base;
        ok &= mmu::translate (r.base + r.size - 8) == r.base + r.size - 8;
      }
    ok &= mmu::translate (0x40000000ULL) == ~0ULL; // unmapped so far
    report ("software walk: identity, and a hole where expected", ok);
  }

  const std::uint64_t pa = board::SCRATCH_BASE;

  // 3.
  {
    phys (pa) = 0;
    mmu::store_translated (pa, 0x1122334455667788ULL);
    bool ok = phys (pa) == 0x1122334455667788ULL;
    phys (pa + 8) = 0xA5A5A5A55A5A5A5AULL;
    ok &= mmu::load_translated (pa + 8) == 0xA5A5A5A55A5A5A5AULL;
    report ("identity: translated store/load == physical", ok);
  }

  // 4.
  {
    constexpr std::uint64_t VA = 0x40000000ULL;
    bool ok = mmu::map_2m (VA, pa, riscv_board::FLAGS_RAM);
    ok &= mmu::translate (VA + 0x10) == pa + 0x10;

    phys (pa + 0x10) = 0;
    mmu::store_translated (VA + 0x10, 0xC0FFEE00DEADBEEFULL);
    ok &= phys (pa + 0x10) == 0xC0FFEE00DEADBEEFULL;

    phys (pa + 0x18) = 0x0123456789ABCDEFULL;
    ok &= mmu::load_translated (VA + 0x18) == 0x0123456789ABCDEFULL;

    // The alias must not be where the identity map would have put it.
    ok &= mmu::translate (VA) != VA;
    report ("alias 0x40000000 -> scratch DDR, both directions", ok);
  }

  // 5.
  {
    phys (pa + 0x40) = 0;
    phys (pa + 0x48) = 0;
    mmu::store_translated (pa + 0x43, 0x0102030405060708ULL);
    const auto* bytes = reinterpret_cast<volatile std::uint8_t*> (pa + 0x43);
    bool ok = bytes[0] == 0x08 && bytes[7] == 0x01;
    ok &= mmu::load_translated (pa + 0x43) == 0x0102030405060708ULL;
    report ("misaligned 64-bit store/load through the MMU", ok);
  }

  // 6.
  {
    const auto k0 = sysclock.now ();
    sysclock.sleep_for (20);
    report ("scheduler and tick still run", sysclock.now () - k0 >= 20);
  }

  if (all_ok)
    {
      uart::write ("RESULT: PASS (mmu-test)\n");
      hw_result::ok ();
    }
  uart::write ("RESULT: FAIL (mmu-test)\n");
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}

#endif // __riscv_xlen != 64
