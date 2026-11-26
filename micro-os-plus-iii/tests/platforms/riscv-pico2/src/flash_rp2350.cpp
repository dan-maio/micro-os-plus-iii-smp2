/*
 * flash_rp2350.cpp -- RAM-resident QSPI flash writer (include/flash_rp2350.h).
 *
 * The pico-sdk's RP2350 sequence (hardware_flash/flash.c), on the Hazard3
 * cores:
 *   look the ROM functions up (RISC-V: the lookup entry's 16-bit address at
 *   0x7dfa, RT_FLAG_FUNC_RISCV)
 *   MIE off; clean the XIP cache (pending PSRAM writes); save the QSPI pads,
 *   QMI M0 timing and the M1 (CS1, PSRAM) registers
 *   connect_internal_flash; flash_exit_xip; erase | program; flush_cache
 *   re-enter XIP with the boot ROM's XIP setup stub (a copy of boot RAM, as
 *   the SDK's flash_enable_xip_via_boot2), or flash_enter_cmd_xip if the stub
 *   is empty
 *   restore pads, M0 timing (our clock's divider) and M1; MIE back
 * Between exit_xip and the re-entry nothing may come from flash or PSRAM:
 * these functions live in .data.ramfunc (SRAM on every linker script: .data
 * on linker.ld, .psram_ram on linker-psram.ld), call only the ROM, the stub
 * copy and inline code, and keep their state in SRAM.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <flash_rp2350.h>

#include <cstdint>

#define RAMFUNC __attribute__ ((noinline, section (".data.ramfunc"), used))
#define RAMDATA __attribute__ ((section (".data.ramfunc.d")))

namespace
{
  constexpr std::uint32_t ROM_LOOKUP_ENTRY = 0x7dfcu - 2u; // RISC-V
  constexpr std::uint32_t RT_FLAG_FUNC_RISCV = 0x0001u;

  constexpr std::uint32_t
  rom_code (char a, char b)
  {
    return static_cast<std::uint32_t> (a)
           | (static_cast<std::uint32_t> (b) << 8);
  }

  constexpr std::uint32_t QMI_BASE = 0x400d0000u;
  constexpr std::uint32_t QMI_M0_TIMING = QMI_BASE + 0x0cu;
  constexpr std::uint32_t QMI_M1_TIMING = QMI_BASE + 0x20u;
  constexpr std::uint32_t QMI_M1_RFMT = QMI_BASE + 0x24u;
  constexpr std::uint32_t QMI_M1_RCMD = QMI_BASE + 0x28u;
  constexpr std::uint32_t QMI_M1_WFMT = QMI_BASE + 0x2cu;
  constexpr std::uint32_t QMI_M1_WCMD = QMI_BASE + 0x30u;
  constexpr std::uint32_t PADS_QSPI_IO = 0x40040000u + 0x04u; // io[0..5]
  constexpr std::uint32_t XIP_MAINTENANCE = 0x18000000u;
  constexpr std::uint32_t XIP_SPACE = 0x04000000u; // XIP_END - XIP_BASE
  constexpr std::uint32_t XIP_CACHE_SIZE = 16u * 1024u;
  constexpr std::uint32_t XIP_CACHE_LINE = 8u;
  constexpr std::uint32_t OP_INVALIDATE_SET_WAY = 0u, OP_CLEAN_SET_WAY = 1u;
  constexpr std::uint32_t BOOTRAM_BASE = 0x400e0000u;
  constexpr unsigned STUB_WORDS = 64;

  using lookup_fn = void* (*)(std::uint32_t code, std::uint32_t mask);
  using void_fn = void (*) (void);
  using erase_fn = void (*) (std::uint32_t, std::size_t, std::uint32_t,
                             std::uint8_t);
  using program_fn = void (*) (std::uint32_t, const std::uint8_t*,
                               std::size_t);

  __attribute__ ((always_inline)) inline volatile std::uint32_t&
  reg (std::uint32_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
  __attribute__ ((always_inline)) inline void*
  rom_func (std::uint32_t code)
  {
    const auto lookup = reinterpret_cast<lookup_fn> (static_cast<std::uintptr_t> (
        *reinterpret_cast<const volatile std::uint16_t*> (ROM_LOOKUP_ENTRY)));
    return lookup (code, RT_FLAG_FUNC_RISCV);
  }
#pragma GCC diagnostic pop

  __attribute__ ((always_inline)) inline std::uint32_t
  irq_off (void)
  {
    std::uint32_t s;
    __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(s)::"memory");
    return s;
  }

  __attribute__ ((always_inline)) inline void
  irq_restore (std::uint32_t s)
  {
    if (s & 8u)
      {
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
      }
  }

  __attribute__ ((always_inline)) inline void
  cache_maintain (std::uint32_t op)
  {
    for (std::uint32_t off = XIP_SPACE - XIP_CACHE_SIZE; off < XIP_SPACE;
         off += XIP_CACHE_LINE)
      {
        *reinterpret_cast<volatile std::uint8_t*> (XIP_MAINTENANCE + off + op)
            = 0;
      }
    __asm__ volatile("fence iorw, iorw\n fence.i" ::: "memory");
  }

  struct saved
  {
    std::uint32_t pads[6];
    std::uint32_t m0_timing;
    std::uint32_t m1[5];
  };

  // The boot ROM's XIP setup stub, copied out of boot RAM once (SRAM, run
  // as code: SRAM is executable).
  std::uint32_t g_stub[STUB_WORDS] RAMDATA __attribute__ ((aligned (4)));
  std::uint32_t g_stub_state RAMDATA; // 0 unread, 1 valid, 2 empty
  saved g_saved RAMDATA;

  __attribute__ ((always_inline)) inline void
  stub_copyout (void)
  {
    if (g_stub_state != 0u)
      {
        return;
      }
    std::uint32_t any = 0;
    for (unsigned i = 0; i < STUB_WORDS; ++i)
      {
        g_stub[i] = reg (BOOTRAM_BASE + 4u * i);
        any |= g_stub[i];
      }
    __asm__ volatile("fence.i" ::: "memory");
    g_stub_state = (any != 0u) ? 1u : 2u;
  }

  __attribute__ ((always_inline)) inline void
  save_state (void)
  {
    for (unsigned i = 0; i < 6; ++i)
      {
        g_saved.pads[i] = reg (PADS_QSPI_IO + 4u * i);
      }
    g_saved.m0_timing = reg (QMI_M0_TIMING);
    g_saved.m1[0] = reg (QMI_M1_TIMING);
    g_saved.m1[1] = reg (QMI_M1_RFMT);
    g_saved.m1[2] = reg (QMI_M1_RCMD);
    g_saved.m1[3] = reg (QMI_M1_WFMT);
    g_saved.m1[4] = reg (QMI_M1_WCMD);
  }

  __attribute__ ((always_inline)) inline void
  restore_state (void)
  {
    for (unsigned i = 0; i < 6; ++i)
      {
        reg (PADS_QSPI_IO + 4u * i) = g_saved.pads[i];
      }
    // The re-entry set the flash's read format; keep it, but with the
    // divider our clock needs (clocks.cpp), not the boot clock's.
    reg (QMI_M0_TIMING) = g_saved.m0_timing;
    // CS1 (PSRAM): the ROM reset the QMI registers at exit_xip without
    // touching the device (no CS1 in FLASH_DEVINFO): the old config is still
    // right (the SDK's "case 1").
    reg (QMI_M1_TIMING) = g_saved.m1[0];
    reg (QMI_M1_RFMT) = g_saved.m1[1];
    reg (QMI_M1_RCMD) = g_saved.m1[2];
    reg (QMI_M1_WFMT) = g_saved.m1[3];
    reg (QMI_M1_WCMD) = g_saved.m1[4];
    __asm__ volatile("fence iorw, iorw\n fence.i" ::: "memory");
  }

  __attribute__ ((always_inline)) inline void
  xip_reenter (void_fn enter_cmd_xip)
  {
    if (g_stub_state == 1u)
      {
        reinterpret_cast<void_fn> (reinterpret_cast<std::uintptr_t> (g_stub)) ();
        mu_flash_xip_via_stub = 1u;
      }
    else
      {
        enter_cmd_xip ();
        mu_flash_xip_via_stub = 0u;
      }
  }
} // namespace

extern "C"
{
  volatile std::uint32_t mu_flash_xip_via_stub RAMDATA;

  RAMFUNC void
  mu_flash_erase (std::uint32_t offset, std::size_t bytes)
  {
    const auto connect = reinterpret_cast<void_fn> (rom_func (rom_code ('I', 'F')));
    const auto exit_xip = reinterpret_cast<void_fn> (rom_func (rom_code ('E', 'X')));
    const auto erase = reinterpret_cast<erase_fn> (rom_func (rom_code ('R', 'E')));
    const auto flush = reinterpret_cast<void_fn> (rom_func (rom_code ('F', 'C')));
    const auto enter = reinterpret_cast<void_fn> (rom_func (rom_code ('C', 'X')));

    const std::uint32_t s = irq_off ();
    stub_copyout ();
    cache_maintain (OP_CLEAN_SET_WAY);
    save_state ();
    connect ();
    exit_xip ();
    erase (offset, bytes, 0x10000u, 0xD8u); // 64 KiB block erase where it fits
    flush ();
    xip_reenter (enter);
    restore_state ();
    irq_restore (s);
  }

  RAMFUNC void
  mu_flash_program (std::uint32_t offset, const void* data, std::size_t bytes)
  {
    const auto connect = reinterpret_cast<void_fn> (rom_func (rom_code ('I', 'F')));
    const auto exit_xip = reinterpret_cast<void_fn> (rom_func (rom_code ('E', 'X')));
    const auto program = reinterpret_cast<program_fn> (rom_func (rom_code ('R', 'P')));
    const auto flush = reinterpret_cast<void_fn> (rom_func (rom_code ('F', 'C')));
    const auto enter = reinterpret_cast<void_fn> (rom_func (rom_code ('C', 'X')));

    const std::uint32_t s = irq_off ();
    stub_copyout ();
    cache_maintain (OP_CLEAN_SET_WAY);
    save_state ();
    connect ();
    exit_xip ();
    program (offset, static_cast<const std::uint8_t*> (data), bytes);
    flush ();
    xip_reenter (enter);
    restore_state ();
    irq_restore (s);
  }

  RAMFUNC void
  mu_flash_xip_flush (void)
  {
    const std::uint32_t s = irq_off ();
    cache_maintain (OP_CLEAN_SET_WAY);
    cache_maintain (OP_INVALIDATE_SET_WAY);
    irq_restore (s);
  }

  // On the other hart: nothing but the two flags in SRAM, MIE off (the
  // trap vector is in flash).
  RAMFUNC void
  mu_flash_core1_lockout (volatile std::uint32_t* ack,
                          volatile std::uint32_t* release)
  {
    const std::uint32_t s = irq_off ();
    *ack = 1u;
    __asm__ volatile("fence rw, rw" ::: "memory");
    while (*release == 0u)
      {
        __asm__ volatile("fence r, r" ::: "memory");
      }
    *ack = 0u;
    __asm__ volatile("fence rw, rw" ::: "memory");
    irq_restore (s);
  }
}
