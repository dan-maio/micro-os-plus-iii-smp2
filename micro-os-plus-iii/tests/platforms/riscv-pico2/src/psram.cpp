/*
 * psram.cpp -- RP2350 QSPI PSRAM driver (QMI M1, CS1), port of the Cortex-M
 * pico2 board's glue/psram.cpp (itself a port of the pico-sdk PSRAM
 * examples). Built with PICO2_PSRAM (data in the PSRAM: start-up copies
 * the routines and runs init() before .data) or PICO2_PSRAM_COPY (start-up
 * only copies them; the application calls psram::init() itself -- the
 * Cortex-M board's option of the same name, used by psram-exec).
 *
 * init() and read_id() are in .psram_ram, linked into SRAM and copied there
 * from flash by riscv_board_initialize_memory() before they are called: while
 * the QMI is in direct mode (driving CS1) it cannot serve flash XIP, so code
 * fetched from flash then would stall the bus for good. Everything they call
 * is always-inlined for the same reason, and machine interrupts are off for
 * the whole sequence (a handler would be fetched from flash).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#if defined(PICO2_PSRAM) || defined(PICO2_PSRAM_COPY)

#include <psram.hpp>

#include <cstdint>

namespace
{
  __attribute__ ((always_inline)) inline volatile std::uint32_t&
  reg (std::uint32_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }

  constexpr std::uint32_t QMI_BASE = 0x400d0000u;
  constexpr std::uint32_t XIP_CTRL = 0x400c8000u;
  constexpr std::uint32_t IO_BANK0_BASE = 0x40028000u;
  constexpr std::uint32_t PADS_BANK0 = 0x40038000u;

  constexpr std::uint32_t QMI_DIRECT_CSR = QMI_BASE + 0x00u;
  constexpr std::uint32_t QMI_DIRECT_TX = QMI_BASE + 0x04u;
  constexpr std::uint32_t QMI_DIRECT_RX = QMI_BASE + 0x08u;
  constexpr std::uint32_t QMI_M1_TIMING = QMI_BASE + 0x20u;
  constexpr std::uint32_t QMI_M1_RFMT = QMI_BASE + 0x24u;
  constexpr std::uint32_t QMI_M1_RCMD = QMI_BASE + 0x28u;
  constexpr std::uint32_t QMI_M1_WFMT = QMI_BASE + 0x2cu;
  constexpr std::uint32_t QMI_M1_WCMD = QMI_BASE + 0x30u;

  constexpr std::uint32_t D_EN = 0x00000001u;
  constexpr std::uint32_t D_BUSY = 0x00000002u;
  constexpr std::uint32_t D_CS1N = 0x00000008u;
  constexpr std::uint32_t D_CLKDIV = 30u << 22; // ~5 MHz at 150 MHz

  constexpr std::uint32_t GPIO_FUNC_XIP_CS1 = 9u;
  constexpr std::uint32_t PADS_ISO = 1u << 8;
  constexpr std::uint32_t PADS_IE = 1u << 6;

#if !defined(PICO2_PSRAM_CS_PIN)
#define PICO2_PSRAM_CS_PIN 0
#endif
  constexpr std::uint32_t CS_PIN = PICO2_PSRAM_CS_PIN;
  constexpr std::uint32_t CS_GPIO_CTRL = IO_BANK0_BASE + CS_PIN * 8 + 4;
  constexpr std::uint32_t CS_PAD_CTRL = PADS_BANK0 + 0x04 + CS_PIN * 4;

  constexpr std::uint8_t CMD_RSTEN = 0x66;
  constexpr std::uint8_t CMD_RST = 0x99;
  constexpr std::uint8_t CMD_QUAD_EN = 0x35;
  constexpr std::uint8_t CMD_QUAD_READ = 0xEB;
  constexpr std::uint8_t CMD_QUAD_WRITE = 0x38;

  // Pop one byte from the direct-mode RX FIFO. A real load: in C++
  // `(void) reg (...)` on a volatile reference does not read (GCC warns
  // "conversion to void will not access object"), so the Cortex-M original
  // never popped the FIFO.
  __attribute__ ((always_inline)) inline void
  drain_rx (void)
  {
    const std::uint32_t v = reg (QMI_DIRECT_RX);
    __asm__ volatile("" ::"r"(v));
  }

  __attribute__ ((always_inline)) inline bool
  wait_until (volatile std::uint32_t& csr, std::uint32_t mask, bool set)
  {
    for (std::uint32_t n = 0; n < 100000u; ++n)
      {
        if (((csr & mask) != 0) == set)
          {
            return true;
          }
      }
    return false;
  }

  // mstatus.MIE off; the previous mstatus, for irq_restore().
  __attribute__ ((always_inline)) inline std::uint32_t
  irq_save (void)
  {
    std::uint32_t s;
    __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(s)::"memory");
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    return s;
  }

  __attribute__ ((always_inline)) inline void
  irq_restore (std::uint32_t s)
  {
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    if (s & 8u)
      {
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
      }
  }

  __attribute__ ((always_inline)) inline void
  cs_pin_to_qmi (void)
  {
    reg (CS_GPIO_CTRL) = (reg (CS_GPIO_CTRL) & ~0x1Fu) | GPIO_FUNC_XIP_CS1;
    reg (CS_PAD_CTRL) = (reg (CS_PAD_CTRL) & ~PADS_ISO) | PADS_IE;
  }
} // namespace

namespace psram
{
  bool init (void) __attribute__ ((section (".psram_ram"), noinline));
  void read_id (std::uint8_t out[8])
      __attribute__ ((section (".psram_ram"), noinline));

  bool
  init (void)
  {
    const std::uint32_t s = irq_save ();

    // 1. The CS pin to XIP CS1, pad un-isolated.
    cs_pin_to_qmi ();

    // 2. QMI direct mode (slow clock); wait out the last XIP transfer.
    reg (QMI_DIRECT_CSR) = D_CLKDIV | D_EN;
    wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);

    // 3. Reset, then quad enable, one CS1 frame per byte. No quad-exit
    //    (0xF5) frame: it left a byte in DIRECT_RX that no read popped and
    //    wedged the interface (measured on the Cortex-M board); RSTEN/RST
    //    puts the device back in SPI mode whatever its state.
    for (std::uint32_t i = 0; i < 34; ++i)
      {
        reg (QMI_DIRECT_CSR) |= D_CS1N;
        const std::uint32_t cmd
            = (i == 0) ? CMD_RSTEN : (i == 1) ? CMD_RST : CMD_QUAD_EN;
        reg (QMI_DIRECT_TX) = cmd;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        reg (QMI_DIRECT_CSR) &= ~D_CS1N;
        for (volatile std::uint32_t j = 0; j < 20; ++j)
          {
          }
        drain_rx ();
      }

    // 4. Leave direct mode.
    reg (QMI_DIRECT_CSR) &= ~(D_CS1N | D_EN);

    // 5. M1 timing: SCK = clk_sys / 2 (75 MHz at 150 MHz).
    reg (QMI_M1_TIMING) = (0x2u << 28)   // PAGEBREAK 1 KB
                          | (3u << 23)   // SELECT_HOLD
                          | (1u << 30)   // COOLDOWN
                          | (1u << 8)    // RXDELAY
                          | (14u << 17)  // MAX_SELECT ~8 us
                          | (6u << 12)   // MIN_DESELECT
                          | (2u << 0);   // CLKDIV

    // 6. Read: 0xEB quad, 8-bit prefix, 24 dummy bits, all quad.
    reg (QMI_M1_RFMT) = (0x2u << 0) | (0x2u << 2) | (0x2u << 4) | (0x2u << 6)
                        | (0x2u << 8) | (0x1u << 12) | (0x0u << 14)
                        | (0x6u << 16);
    reg (QMI_M1_RCMD) = CMD_QUAD_READ;

    // 7. Write: 0x38 quad, 8-bit prefix, no dummy.
    reg (QMI_M1_WFMT) = (0x2u << 0) | (0x2u << 2) | (0x2u << 4) | (0x2u << 6)
                        | (0x2u << 8) | (0x1u << 12) | (0x0u << 14)
                        | (0x0u << 16);
    reg (QMI_M1_WCMD) = CMD_QUAD_WRITE;

    // 8. Window 1 writable (XIP_CTRL.WRITABLE_M1).
    reg (XIP_CTRL) |= 0x800u;

    // 9. A word written and read back.
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*> (BASE);
    *p = 0xA5A5A5A5u;
    __asm__ volatile("fence iorw, iorw" ::: "memory");
    const bool ok = (*p == 0xA5A5A5A5u);

    irq_restore (s);
    return ok;
  }

  void
  read_id (std::uint8_t out[8])
  {
    const std::uint32_t s = irq_save ();

    cs_pin_to_qmi ();
    reg (QMI_DIRECT_CSR) = D_CLKDIV | D_EN;
    wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);

    for (std::uint32_t i = 0; i < 2; ++i)
      {
        reg (QMI_DIRECT_CSR) |= D_CS1N;
        reg (QMI_DIRECT_TX) = (i == 0) ? CMD_RSTEN : CMD_RST;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        reg (QMI_DIRECT_CSR) &= ~D_CS1N;
        for (volatile std::uint32_t j = 0; j < 200; ++j)
          {
          }
        drain_rx ();
      }

    // 0x9F + 3 address bytes, then 8 bytes in, CS1 held for the frame.
    reg (QMI_DIRECT_CSR) |= D_CS1N;
    for (std::uint32_t i = 0; i < 4; ++i)
      {
        reg (QMI_DIRECT_TX) = (i == 0) ? 0x9Fu : 0x00u;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        drain_rx ();
      }
    for (std::uint32_t i = 0; i < 8; ++i)
      {
        reg (QMI_DIRECT_TX) = 0x00;
        wait_until (reg (QMI_DIRECT_CSR), D_BUSY, false);
        out[i] = static_cast<std::uint8_t> (reg (QMI_DIRECT_RX) & 0xFFu);
      }
    reg (QMI_DIRECT_CSR) &= ~(D_CS1N | D_EN);

    irq_restore (s);
  }
} // namespace psram

extern "C"
{
  // linker-psram.ld: .psram_ram's load (flash) and run (SRAM) addresses.
  extern std::uint32_t __psram_ram_load[];
  extern std::uint32_t __psram_ram_start[];
  extern std::uint32_t __psram_ram_end[];

  void riscv_board_initialize_memory (void);
  // Set to 1 when init() failed; startup never reaches .data then. Kept in
  // SRAM (.noinit) so a debugger can read it.
  std::uint32_t riscv_psram_failed __attribute__ ((section (".noinit")));
}

// startup.S calls this on the boot stack (SRAM) before .data is copied and
// .bss zeroed -- both are in the PSRAM. It runs from flash: XIP is normal
// until init() enters direct mode, and init() itself runs from SRAM.
void
riscv_board_initialize_memory (void)
{
  for (std::uint32_t *s = __psram_ram_load, *d = __psram_ram_start;
       d < __psram_ram_end;)
    {
      *d++ = *s++;
    }
  __asm__ volatile("fence.i" ::: "memory");

#if defined(PICO2_PSRAM)
  if (!psram::init ())
    {
      riscv_psram_failed = 1;
      // No .data, no .bss: nothing else can run. Say so over semihosting
      // and end the run (hw.sh: FAIL).
      static const char msg[] = "RESULT: FAIL (PSRAM init: no read-back at 0x11000000)\n";
      register std::uint32_t a0 __asm__ ("a0") = 0x04; // SYS_WRITE0
      register const char* a1 __asm__ ("a1") = msg;
      __asm__ volatile(".option push\n .option norvc\n .balign 16\n"
                       " slli x0, x0, 0x1f\n ebreak\n srai x0, x0, 7\n"
                       ".option pop"
                       : "+r"(a0)
                       : "r"(a1)
                       : "memory");
      register std::uint32_t b0 __asm__ ("a0") = 0x18;   // SYS_EXIT
      register std::uint32_t b1 __asm__ ("a1") = 0x20023; // RunTimeErrorUnknown
      __asm__ volatile(".option push\n .option norvc\n .balign 16\n"
                       " slli x0, x0, 0x1f\n ebreak\n srai x0, x0, 7\n"
                       ".option pop"
                       : "+r"(b0)
                       : "r"(b1)
                       : "memory");
      for (;;)
        {
        }
    }
#endif // PICO2_PSRAM
}

#endif // PICO2_PSRAM || PICO2_PSRAM_COPY
