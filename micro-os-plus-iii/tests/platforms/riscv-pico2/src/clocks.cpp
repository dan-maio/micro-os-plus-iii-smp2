/*
 * clocks.cpp -- RP2350 clock tree: XOSC 12 MHz -> PLL_SYS -> clk_sys =
 * clk_peri, clk_ref = XOSC; the TICKS generators (1 us from clk_ref):
 * watchdog, TIMER0 and the RISC-V platform timer; the core voltage (POWMAN
 * VREG), clk_adc, and the watchdog reboot into another clock.
 *
 * The Cortex-M pico2 board's clocks.cpp (micro-os-plus-iii-cortexm,
 * test/boards/pico2/src/clocks.cpp), with its build-time clock
 * (PICO2_SYS_CLK_KHZ), its boot-time table (PICO2_CLOCK_FROM_SCRATCH) and
 * the 250 MHz overclock (PICO2_ENABLE_250MHZ). RISC-V differences: fences
 * for the Arm barriers, mstatus.MIE for PRIMASK, a spin for the final wfi.
 *
 * The QSPI dividers are set before the clock changes, and only raised (a
 * raised divider only slows the bus, so it is safe on the boot clock; a
 * lowered one is not -- see qspi_dividers()):
 *   flash (QMI M0)  SCK <= 50 MHz: CLKDIV >= ceil(clk_sys / 50 MHz)
 *   PSRAM (QMI M1)  SCK <= 109 MHz (APS6404): CLKDIV >= ceil(clk_sys /
 *                   109 MHz), and >= the 2 psram::init() set -- 2 up to
 *                   200 MHz, 3 at 250
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <clocks.hpp>
#include <rp2350.hpp>

using rp2350::reg;

namespace
{
  constexpr std::uint32_t XOSC_CTRL = 0x00, XOSC_STATUS = 0x04, XOSC_STARTUP = 0x0C;
  constexpr std::uint32_t PLL_CS = 0x00, PLL_PWR = 0x04, PLL_FBDIV_INT = 0x08,
                          PLL_PRIM = 0x0C;
  constexpr std::uint32_t CLK_REF_CTRL = 0x30, CLK_SYS_CTRL = 0x3C,
                          CLK_PERI_CTRL = 0x48;
  constexpr std::uint32_t CLK_DIV = 0x04, CLK_SELECTED = 0x08;

  constexpr std::uint32_t XOSC_FREQ_1_15MHZ = 0x0AA0, XOSC_ENABLE = 0x0FAB;
  constexpr std::uint32_t XOSC_STABLE = 1u << 31;
  constexpr std::uint32_t PLL_PWR_PD = 1u << 0, PLL_PWR_POSTDIVPD = 1u << 3,
                          PLL_PWR_VCOPD = 1u << 5, PLL_CS_LOCK = 1u << 31;
  constexpr std::uint32_t CLK_REF_SRC = 0x3, CLK_REF_SRC_XOSC = 2;
  constexpr std::uint32_t CLK_SYS_SRC = 0x3, CLK_SYS_SRC_AUX = 1;
  constexpr std::uint32_t CLK_SYS_AUX = 0xE0, CLK_SYS_AUX_LSB = 5;
  constexpr std::uint32_t CLK_REF_AUX = 0x60, CLK_REF_AUX_LSB = 5;
  constexpr std::uint32_t CLK_PERI_AUX = 0xE0, CLK_PERI_AUX_LSB = 5,
                          CLK_PERI_ENABLE = 1u << 11;
  constexpr std::uint32_t RESET_PLL_SYS = 1u << 14, RESET_IO_BANK0 = 1u << 6,
                          RESET_PADS_BANK0 = 1u << 9;
  constexpr std::uint32_t CLK_DIV_UNDIVIDED = 1u << 16;

#if !defined(PICO2_SYS_CLK_KHZ)
#define PICO2_SYS_CLK_KHZ 150000
#endif
#if PICO2_SYS_CLK_KHZ == 150000
  constexpr std::uint32_t CT_FBDIV = 125, CT_PD1 = 5, CT_PD2 = 2; // VCO 1500
  constexpr std::uint8_t CT_VSEL = 0x0b;                          // 1.10 V
#elif PICO2_SYS_CLK_KHZ == 120000
  constexpr std::uint32_t CT_FBDIV = 100, CT_PD1 = 5, CT_PD2 = 2; // VCO 1200
  constexpr std::uint8_t CT_VSEL = 0x0b;
#elif PICO2_SYS_CLK_KHZ == 100000
  constexpr std::uint32_t CT_FBDIV = 125, CT_PD1 = 5, CT_PD2 = 3; // VCO 1500
  constexpr std::uint8_t CT_VSEL = 0x0b;
#elif PICO2_SYS_CLK_KHZ == 200000
  constexpr std::uint32_t CT_FBDIV = 100, CT_PD1 = 6, CT_PD2 = 1; // VCO 1200
  constexpr std::uint8_t CT_VSEL = 0x0d;                          // 1.20 V
#elif PICO2_SYS_CLK_KHZ == 250000
  constexpr std::uint32_t CT_FBDIV = 125, CT_PD1 = 6, CT_PD2 = 1; // VCO 1500
  constexpr std::uint8_t CT_VSEL = 0x0e;                          // 1.25 V
#elif PICO2_SYS_CLK_KHZ == 48000
  constexpr std::uint32_t CT_FBDIV = 100, CT_PD1 = 5, CT_PD2 = 5; // VCO 1200
  constexpr std::uint8_t CT_VSEL = 0x0b;
#else
#error "unsupported PICO2_SYS_CLK_KHZ (250000 / 200000 / 150000 / 120000 / 100000 / 48000)"
#endif

  constexpr std::uint32_t WD_CTRL = 0x00, WD_LOAD = 0x04, WD_SCRATCH0 = 0x0C,
                          WD_SCRATCH1 = 0x10, WD_SCRATCH4 = 0x1C;
  constexpr std::uint32_t SCRATCH_MAGIC = 0xC10C4B01u;

  // What init() selected.
  std::uint32_t g_khz = PICO2_SYS_CLK_KHZ;
  std::uint8_t g_vsel = 0x0b;
  unsigned g_index = 0xFFFFu;

  void
  reset_block (std::uint32_t mask)
  {
    reg (rp2350::RESETS_BASE + 0x00) |= mask;
    __asm__ volatile("nop\n nop" ::: "memory");
  }

  void
  xosc_init (void)
  {
    reg (rp2350::XOSC_BASE + XOSC_STARTUP) = 0xC4;
    reg (rp2350::XOSC_BASE + XOSC_CTRL) = (XOSC_ENABLE << 12) | XOSC_FREQ_1_15MHZ;
    while (!(reg (rp2350::XOSC_BASE + XOSC_STATUS) & XOSC_STABLE))
      {
      }
  }

  void
  pll_sys_program (std::uint32_t fbdiv, std::uint32_t pd1, std::uint32_t pd2)
  {
    const std::uint32_t b = rp2350::PLL_SYS_BASE;
    reset_block (RESET_PLL_SYS);
    rp2350::unreset_block (RESET_PLL_SYS);
    reg (b + PLL_CS) = 1; // REFDIV = 1
    reg (b + PLL_FBDIV_INT) = fbdiv;
    reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    while (!(reg (b + PLL_CS) & PLL_CS_LOCK))
      {
      }
    reg (b + PLL_PRIM) = (pd1 << 16) | (pd2 << 12);
    reg (b + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;
  }

  void
  clk_configure (std::uint32_t ctrl, std::uint32_t src_mask,
                 std::uint32_t aux_mask, std::uint32_t aux_lsb,
                 std::uint32_t src, std::uint32_t auxsrc)
  {
    if (src == CLK_SYS_SRC_AUX)
      {
        reg (ctrl) &= ~src_mask;
        while (!(reg (ctrl + CLK_SELECTED) & 1u))
          {
          }
      }
    reg (ctrl) = (reg (ctrl) & ~aux_mask) | (auxsrc << aux_lsb);
    reg (ctrl) = (reg (ctrl) & ~src_mask) | src;
    while (!(reg (ctrl + CLK_SELECTED) & (1u << src)))
      {
      }
    reg (ctrl + CLK_DIV) = CLK_DIV_UNDIVIDED;
  }

  std::uint32_t
  div_ceil (std::uint32_t a, std::uint32_t b)
  {
    return (a + b - 1u) / b;
  }

  // The QSPI dividers for a clk_sys of `khz` (see the top). On the boot
  // clock, before the clock goes up.
  void
  qspi_dividers (std::uint32_t khz)
  {
    constexpr std::uint32_t QMI_M0_TIMING = rp2350::QMI_BASE + 0x0Cu;
    // Only ever RAISED here: this runs on the boot clock, and a divider below
    // the boot ROM's runs the flash too fast for that clock (measured: 48 MHz
    // asked for 1, the next fetch from flash was garbage -- mcause 2 in
    // clocks::init(), even the trap handler unreadable).
    std::uint32_t d0 = div_ceil (khz, 50000u);
    const std::uint32_t cur0 = reg (QMI_M0_TIMING) & 0xFFu;
    if (d0 < cur0)
      {
        d0 = cur0;
      }
    reg (QMI_M0_TIMING) = (reg (QMI_M0_TIMING) & ~0xFFu) | d0;
#if defined(PICO2_PSRAM)
    // psram::init() (riscv_board_initialize_memory, earlier) set 2.
    constexpr std::uint32_t QMI_M1_TIMING = rp2350::QMI_BASE + 0x20u;
    std::uint32_t d1 = div_ceil (khz, 109000u);
    const std::uint32_t cur1 = reg (QMI_M1_TIMING) & 0xFFu;
    if (d1 < cur1)
      {
        d1 = cur1;
      }
    reg (QMI_M1_TIMING) = (reg (QMI_M1_TIMING) & ~0xFFu) | d1;
#endif
    __asm__ volatile("fence iorw, iorw" ::: "memory");
  }

  // clk_sys = clk_peri = PLL_SYS(fbdiv, pd1, pd2), clk_ref = XOSC, TICKS.
  void
  bring_up_clock_tree (std::uint32_t fbdiv, std::uint32_t pd1,
                       std::uint32_t pd2)
  {
    const std::uint32_t c = rp2350::CLOCKS_BASE;
    xosc_init ();

    // Move clk_sys / clk_ref off any AUX source before touching the PLL.
    reg (c + CLK_SYS_CTRL) &= ~CLK_SYS_SRC;
    while (!(reg (c + CLK_SYS_CTRL + CLK_SELECTED) & 1u))
      {
      }
    reg (c + CLK_REF_CTRL) &= ~CLK_REF_SRC;
    while (!(reg (c + CLK_REF_CTRL + CLK_SELECTED) & 1u))
      {
      }

    pll_sys_program (fbdiv, pd1, pd2);

    clk_configure (c + CLK_REF_CTRL, CLK_REF_SRC, CLK_REF_AUX, CLK_REF_AUX_LSB,
                   CLK_REF_SRC_XOSC, 0);
    clk_configure (c + CLK_SYS_CTRL, CLK_SYS_SRC, CLK_SYS_AUX, CLK_SYS_AUX_LSB,
                   CLK_SYS_SRC_AUX, 0 /* AUXSRC = PLL_SYS */);

    // clk_peri = clk_sys: the UART's reference clock.
    reg (c + CLK_PERI_CTRL) &= ~CLK_PERI_ENABLE;
    reg (c + CLK_PERI_CTRL)
        = (reg (c + CLK_PERI_CTRL) & ~CLK_PERI_AUX) | (0u << CLK_PERI_AUX_LSB);
    reg (c + CLK_PERI_CTRL + CLK_DIV) = CLK_DIV_UNDIVIDED;
    reg (c + CLK_PERI_CTRL) |= CLK_PERI_ENABLE;

    rp2350::unreset_block (RESET_IO_BANK0 | RESET_PADS_BANK0);

    // Tick generators: 12 clk_ref cycles = 1 us, whatever clk_sys is.
    reg (rp2350::TICKS_BASE + 0x34) = 12; // WATCHDOG_CYCLES
    reg (rp2350::TICKS_BASE + 0x30) = 1;  // WATCHDOG_CTRL.ENABLE
    reg (rp2350::TICKS_BASE + 0x1C) = 12; // TIMER0_CYCLES
    reg (rp2350::TICKS_BASE + 0x18) = 1;  // TIMER0_CTRL.ENABLE
    reg (rp2350::TICKS_RISCV_CYCLES) = 12;
    reg (rp2350::TICKS_RISCV_CTRL) = 1;
  }
} // namespace

namespace clocks
{
  // clk_sys = 12 MHz * fbdiv / pd1 / pd2; vsel 0x0b = 1.10 V (<= 150 MHz),
  // 0x0c = 1.15 V (176/192), 0x0d = 1.20 V (200), 0x0e = 1.25 V (250).
  const sys_entry SYS_TABLE[SYS_TABLE_LEN] = {
    { 48000, 100, 5, 5, 0x0b },  // VCO 1200
    { 60000, 125, 5, 5, 0x0b },  // VCO 1500
    { 72000, 120, 5, 4, 0x0b },  // VCO 1440
    { 84000, 98, 7, 2, 0x0b },   // VCO 1176
    { 96000, 96, 4, 3, 0x0b },   // VCO 1152
    { 108000, 108, 6, 2, 0x0b }, // VCO 1296
    { 120000, 100, 5, 2, 0x0b }, // VCO 1200
    { 132000, 110, 5, 2, 0x0b }, // VCO 1320
    { 150000, 125, 5, 2, 0x0b }, // VCO 1500
    { 176000, 88, 6, 1, 0x0c },  // VCO 1056, 1.15 V
    { 192000, 96, 6, 1, 0x0c },  // VCO 1152, 1.15 V
    { 200000, 100, 6, 1, 0x0d }, // VCO 1200, 1.20 V
#if defined(PICO2_ENABLE_250MHZ)
    { 250000, 125, 6, 1, 0x0e }, // VCO 1500, 1.25 V -- overclock
#endif
  };

  void
  init (void)
  {
#if defined(PICO2_CLOCK_FROM_SCRATCH)
    // The row reboot_to_index() left in the watchdog scratch; row 0 (48 MHz)
    // on a cold boot -- the potentiometer rests at 0 on power-up.
    unsigned idx = 0;
    if (reg (rp2350::WATCHDOG_BASE + WD_SCRATCH1) == SCRATCH_MAGIC)
      {
        const std::uint32_t s = reg (rp2350::WATCHDOG_BASE + WD_SCRATCH0);
        if (s < SYS_TABLE_LEN)
          {
            idx = static_cast<unsigned> (s);
          }
      }
    const sys_entry& e = SYS_TABLE[idx];
    // Voltage and QSPI dividers first, still on the boot clock.
    vreg_set (e.vsel);
    qspi_dividers (e.khz);
    bring_up_clock_tree (e.fbdiv, e.pd1, e.pd2);
    g_index = idx;
    g_khz = e.khz;
    g_vsel = e.vsel;
#else
    if (CT_VSEL > 0x0b)
      {
        vreg_set (CT_VSEL);
      }
    qspi_dividers (PICO2_SYS_CLK_KHZ);
    bring_up_clock_tree (CT_FBDIV, CT_PD1, CT_PD2);
    g_khz = PICO2_SYS_CLK_KHZ;
    g_vsel = CT_VSEL;
#endif
  }

  unsigned
  current_index (void)
  {
    return g_index;
  }

  std::uint32_t
  current_khz (void)
  {
    return g_khz;
  }

  std::uint8_t
  current_vsel (void)
  {
    return g_vsel;
  }

  std::uint32_t
  vsel_to_mv (std::uint8_t vsel)
  {
    // POWMAN VSEL: 0.55 V + 50 mV per step (0x0b = 1.10 V, 0x0f = 1.30 V).
    return 550u + 50u * static_cast<std::uint32_t> (vsel);
  }

  void
  vreg_set (std::uint8_t vsel)
  {
    constexpr std::uint32_t PASSWD = 0x5AFE0000u;
    constexpr std::uint32_t VREG_CTRL = 0x04, VREG = 0x0C;
    constexpr std::uint32_t UNLOCK = 1u << 13;
    constexpr std::uint32_t VSEL_LSB = 4, VSEL_BITS = 0x1F0u;
    constexpr std::uint32_t UPDATE_IN_PROGRESS = 1u << 15;
    const std::uint32_t p = rp2350::POWMAN_BASE;

    // Unlock (needed only above 1.30 V, harmless otherwise); every POWMAN
    // write carries the password in its upper half.
    reg (p + VREG_CTRL) = PASSWD | (reg (p + VREG_CTRL) & 0xFFFFu) | UNLOCK;
    while (reg (p + VREG) & UPDATE_IN_PROGRESS)
      {
      }
    reg (p + VREG) = PASSWD | (reg (p + VREG) & 0xFFFFu & ~VSEL_BITS)
                     | (static_cast<std::uint32_t> (vsel) << VSEL_LSB);
    while (reg (p + VREG) & UPDATE_IN_PROGRESS)
      {
      }
    // Let the rail settle before the clock goes up.
    for (int i = 0; i < 20000; ++i)
      {
        __asm__ volatile("nop");
      }
  }

  void
  adc_clk_init (void)
  {
    // PLL_USB 48 MHz if it is not running yet, then clk_adc from it.
    const std::uint32_t b = rp2350::PLL_USB_BASE;
    constexpr std::uint32_t RESET_PLL_USB = 1u << 15;
    rp2350::unreset_block (RESET_PLL_USB);
    if (!(reg (b + PLL_CS) & PLL_CS_LOCK))
      {
        reg (b + PLL_CS) = 1;
        reg (b + PLL_FBDIV_INT) = 100; // VCO 1200
        reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
        while (!(reg (b + PLL_CS) & PLL_CS_LOCK))
          {
          }
        reg (b + PLL_PRIM) = (5u << 16) | (5u << 12); // /5 /5 = 48 MHz
        reg (b + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;
      }
    const std::uint32_t c = rp2350::CLOCKS_BASE;
    constexpr std::uint32_t CLK_ADC_CTRL = 0x6C, CLK_ADC_ENABLE = 1u << 11,
                            CLK_ADC_AUX = 0xE0, CLK_ADC_AUX_LSB = 5;
    reg (c + CLK_ADC_CTRL) &= ~CLK_ADC_ENABLE;
    reg (c + CLK_ADC_CTRL + CLK_DIV) = CLK_DIV_UNDIVIDED;
    reg (c + CLK_ADC_CTRL)
        = (reg (c + CLK_ADC_CTRL) & ~CLK_ADC_AUX) | (0u << CLK_ADC_AUX_LSB);
    reg (c + CLK_ADC_CTRL) |= CLK_ADC_ENABLE;
  }

  [[noreturn]] void
  reboot_to_index (unsigned idx)
  {
    const std::uint32_t w = rp2350::WATCHDOG_BASE;

    // Quiet the bus first: this hart's interrupts off, the other core held
    // in reset (PSM FRCE_OFF: PROC0 = bit 23, PROC1 = bit 24), so nothing
    // drives the UART or holds a QMI transaction during the countdown (on
    // the Cortex-M board a busy other core made the reboot come out wedged).
    __asm__ volatile("csrci mstatus, 8" ::: "memory");
    {
      const std::uint32_t self = rp2350::cpu_id () & 1u;
      const std::uint32_t other = self ? (1u << 23) : (1u << 24);
      reg (rp2350::PSM_FRCE_OFF) |= other;
      for (int i = 0; i < 2000; ++i)
        {
          __asm__ volatile("nop");
        }
    }

    reg (w + WD_SCRATCH0) = idx;
    reg (w + WD_SCRATCH1) = SCRATCH_MAGIC;
    reg (w + WD_SCRATCH4) = 0; // no boot ROM reboot vector: boot the flash

    // The watchdog counts on its tick.
    reg (rp2350::TICKS_BASE + 0x34) = 12;
    reg (rp2350::TICKS_BASE + 0x30) = 1;

    // Reset everything except ROSC/XOSC (PSM WDSEL).
    reg (rp2350::PSM_BASE + 0x08) = 0x01FFFFFFu & ~0x0Cu;

    // ENABLE off and the debug PAUSE bits cleared (with OpenOCD attached they
    // would hold the countdown), then ~2 ms and ENABLE.
    std::uint32_t ctrl = reg (w + WD_CTRL);
    ctrl &= ~(1u << 30);
    ctrl &= ~((1u << 26) | (1u << 25) | (1u << 24));
    reg (w + WD_CTRL) = ctrl;
    reg (w + WD_LOAD) = 2000;
    reg (w + WD_CTRL) = ctrl | (1u << 30);
    for (;;)
      {
      }
  }

  // PLL_USB -> clk_usb = 48 MHz for the USB device controller, before
  // tusb_init() (the Cortex-M pico2 board's usb_init(), register for
  // register). Returns true if PLL_USB locked and clk_usb runs.
  bool
  usb_init (void)
  {
    constexpr std::uint32_t PLL_USB_BASE = 0x40058000u;
    constexpr std::uint32_t RESET_PLL_USB = 1u << 15;
    constexpr std::uint32_t RESET_USBCTRL = 1u << 28;
    const std::uint32_t b = PLL_USB_BASE;
    const std::uint32_t c = rp2350::CLOCKS_BASE;
    constexpr std::uint32_t CLK_USB_CTRL = 0x60, CLK_USB_ENABLE = 1u << 11,
                            CLK_USB_AUX = 0xE0, CLK_USB_AUX_LSB = 5;

    // Hold the controller in reset while PLL_USB / clk_usb change; take
    // clk_usb down first; power-cycle PLL_USB so the writes below take
    // effect after a warm reset (a live, locked PLL ignores them).
    reset_block (RESET_USBCTRL);
    reg (c + CLK_USB_CTRL) &= ~CLK_USB_ENABLE;
    reset_block (RESET_PLL_USB);
    rp2350::unreset_block (RESET_PLL_USB);
    reg (b + PLL_CS) = 1;          // REFDIV = 1
    reg (b + PLL_FBDIV_INT) = 100; // VCO = 12 * 100 = 1200 MHz
    reg (b + PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    bool locked = false;
    for (std::uint32_t n = 0; n < 5000000u; ++n)
      {
        if (reg (b + PLL_CS) & PLL_CS_LOCK)
          {
            locked = true;
            break;
          }
      }
    if (!locked)
      {
        return false;
      }
    reg (b + PLL_PRIM) = (5u << 16) | (5u << 12); // /5 /5 = 48 MHz
    reg (b + PLL_PWR) &= ~PLL_PWR_POSTDIVPD;

    // clk_usb = PLL_USB, /1.
    reg (c + CLK_USB_CTRL + CLK_DIV) = CLK_DIV_UNDIVIDED;
    reg (c + CLK_USB_CTRL)
        = (reg (c + CLK_USB_CTRL) & ~CLK_USB_AUX) | (0u << CLK_USB_AUX_LSB);
    reg (c + CLK_USB_CTRL) |= CLK_USB_ENABLE;
    return true;
  }
} // namespace clocks
