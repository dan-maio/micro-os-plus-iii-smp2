/*
 * psram-exec -- RISC-V (Hazard3) port of the Cortex-M pico2-pizero board's
 * psram-exec (micro-os-plus-iii-cortexm; from the predecessor's
 * pizero-psram-exec, -main, -main_1, -main_2): code that RUNS FROM THE PSRAM.
 *
 * A bare supervisor (no kernel; its code in flash, its data in SRAM) brings
 * the PSRAM up (psram::init(), copied to SRAM at start-up by PICO2_PSRAM_COPY),
 * copies two self-contained applications from flash into the PSRAM
 * (A at 0x11000000, B at 0x11400000, psram_app.inc) and switches between
 * them: mtvec = the application's own trap handler, call its entry through a
 * register; the application heartbeats from the PSRAM until ESC (switch) or
 * 'x' (end, PASS). Interactive, as the original.
 *
 * The three board fixes of the original (PSRAM-CODE-EXEC-FIXES.md) are kept:
 *   1. clean + invalidate the XIP cache over the code after writing it (the
 *      PSRAM window is cached; code written as data must reach the fetch
 *      path);
 *   2. copy flash -> SRAM buffer -> PSRAM (never interleave the two QMI
 *      windows), retried until it verifies;
 *   3. M1 at full speed -- here psram::init()'s CLKDIV 2 (75 MHz at 150 MHz).
 *
 * Ends in the semihosting exit through hw_result.
 */

#include <led.hpp>
#include <psram.hpp>
#include <rp2350.hpp>
#include <uart.hpp>

#include <hw_result.hpp>

#include <cstdint>

// Shared with the applications (linked into this image): 2 = 'x' pressed.
volatile unsigned g_psram_stop = 0;

namespace
{
  struct app_header_t
  {
    std::uint32_t magic;
    void (*entry) (void);
    void (*trap) (void);
  };
  constexpr std::uint32_t APP_MAGIC = 0x50534558u; // 'PSEX'

  // RP2350 XIP cache maintenance: an 8-bit write to XIP_MAINTENANCE_BASE +
  // offset (from XIP_BASE) + op per 8-byte line; op 3 = clean, 2 = invalidate.
  void
  xip_clean_invalidate (std::uint32_t xip_addr, unsigned size)
  {
    constexpr std::uint32_t MAINT = 0x18000000u, XIP_BASE = 0x10000000u;
    const std::uint32_t off0 = (xip_addr - XIP_BASE) & ~7u;
    const std::uint32_t off1 = ((xip_addr - XIP_BASE) + size + 7u) & ~7u;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    for (std::uint32_t o = off0; o < off1; o += 8u)
      {
        *reinterpret_cast<volatile std::uint8_t*> (MAINT + o + 3u) = 0;
      }
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    for (std::uint32_t o = off0; o < off1; o += 8u)
      {
        *reinterpret_cast<volatile std::uint8_t*> (MAINT + o + 2u) = 0;
      }
    __asm__ volatile("fence iorw, iorw\n fence.i" ::: "memory");
  }

  void
  puthex (std::uint32_t v)
  {
    char b[8];
    for (int i = 7; i >= 0; --i)
      {
        const unsigned d = (v >> (i * 4)) & 0xFu;
        b[7 - i] = static_cast<char> (d < 10 ? '0' + d : 'a' + d - 10);
      }
    uart::write (b, 8);
  }

  // Staging buffer (SRAM): flash -> here -> PSRAM.
  std::uint8_t g_buf[16384];

  bool
  load_app (const char* name, const char* lma, std::uint8_t* vma, unsigned n,
            char* bss0, char* bss1)
  {
    uart::write ("load app ");
    uart::write (name);
    uart::write (": 0x");
    puthex (n);
    uart::write (" bytes -> 0x");
    puthex (static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (vma)));
    uart::write (" ");
    if (n > sizeof (g_buf))
      {
        uart::write ("TOO BIG\n");
        return false;
      }
    for (unsigned i = 0; i < n; ++i)
      {
        g_buf[i] = static_cast<std::uint8_t> (lma[i]);
      }
    volatile std::uint8_t* dst = vma;
    bool ok = false;
    int att = 0;
    for (; att < 16 && !ok; ++att)
      {
        std::uint32_t s;
        __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(s)::"memory");
        for (unsigned i = 0; i < n; ++i)
          {
            dst[i] = g_buf[i];
          }
        __asm__ volatile("fence iorw, iorw" ::: "memory");
        if (s & 8u)
          {
            __asm__ volatile("csrsi mstatus, 8" ::: "memory");
          }
        xip_clean_invalidate (
            static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (vma)),
            n);
        ok = true;
        for (unsigned i = 0; i < n; ++i)
          {
            if (dst[i] != g_buf[i])
              {
                ok = false;
                break;
              }
          }
      }
    if (ok)
      {
        for (char* p = bss0; p < bss1; ++p)
          {
            *p = 0;
          }
        xip_clean_invalidate (
            static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (bss0)),
            static_cast<unsigned> (bss1 - bss0));
      }
    uart::write (ok ? "OK (" : "FAIL (");
    puthex (static_cast<std::uint32_t> (att));
    uart::write (" attempts)\n");
    return ok;
  }

  // Run the application whose header is at `vma`: its trap handler in mtvec,
  // its entry called through a register; back when it returns.
  void
  run_app (std::uint32_t vma)
  {
    const auto* h = reinterpret_cast<const volatile app_header_t*> (vma);
    std::uint32_t saved;
    __asm__ volatile("csrr %0, mtvec" : "=r"(saved));
    __asm__ volatile("csrw mtvec, %0" ::"r"(
                         reinterpret_cast<std::uintptr_t> (h->trap))
                     : "memory");
    h->entry ();
    __asm__ volatile("csrw mtvec, %0" ::"r"(saved) : "memory");
  }
} // namespace

extern "C"
{
  extern char __psram_a_lma_start[], __psram_a_vma_start[], __psram_a_vma_end[],
      __psram_a_bss_start[], __psram_a_bss_end[];
  extern char __psram_b_lma_start[], __psram_b_vma_start[], __psram_b_vma_end[],
      __psram_b_bss_start[], __psram_b_bss_end[];
}

int
main (void)
{
  led::init ();
  uart::write ("\n=== PSRAM dual-app supervisor (WeAct RP2350B, Hazard3) ===\n");

  const bool ok = psram::init ();
  uart::write ("psram::init(): ");
  uart::write (ok ? "PASS\n" : "FAIL\n");
  if (!ok)
    {
      uart::write ("\nRESULT: FAIL (psram::init)\n");
      hw_result::fail ();
    }

  const bool a_ok = load_app (
      "A", __psram_a_lma_start,
      reinterpret_cast<std::uint8_t*> (__psram_a_vma_start),
      static_cast<unsigned> (__psram_a_vma_end - __psram_a_vma_start),
      __psram_a_bss_start, __psram_a_bss_end);
  const bool b_ok = load_app (
      "B", __psram_b_lma_start,
      reinterpret_cast<std::uint8_t*> (__psram_b_vma_start),
      static_cast<unsigned> (__psram_b_vma_end - __psram_b_vma_start),
      __psram_b_bss_start, __psram_b_bss_end);

  const std::uint32_t apps[2]
      = { static_cast<std::uint32_t> (
              reinterpret_cast<std::uintptr_t> (__psram_a_vma_start)),
          static_cast<std::uint32_t> (
              reinterpret_cast<std::uintptr_t> (__psram_b_vma_start)) };
  bool hdr_ok = true;
  for (const std::uint32_t a : apps)
    {
      if (reinterpret_cast<const volatile app_header_t*> (a)->magic != APP_MAGIC)
        {
          hdr_ok = false;
        }
    }
  if (!a_ok || !b_ok || !hdr_ok)
    {
      uart::write ("\nRESULT: FAIL (application load)\n");
      hw_result::fail ();
    }

  uart::write ("\nBoth apps loaded in PSRAM. Supervisor running.\n");
  uart::write ("Press ESC to switch between APP A and APP B; x to finish.\n");

  int cur = 0;
  unsigned runs[2] = { 0, 0 };
  for (;;)
    {
      g_psram_stop = 0;
      run_app (apps[cur]);
      runs[cur] = runs[cur] + 1;
      if (g_psram_stop == 2u)
        {
          uart::write ("\nRESULT: PASS (both PSRAM apps loaded and ran; runs A=");
          puthex (runs[0]);
          uart::write (" B=");
          puthex (runs[1]);
          uart::write (")\n");
          hw_result::ok ();
        }
      uart::write ("\n=== supervisor: switching apps ===\n");
      cur ^= 1;
    }
}
