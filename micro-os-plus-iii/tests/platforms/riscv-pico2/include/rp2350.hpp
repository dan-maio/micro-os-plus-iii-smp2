/*
 * rp2350.hpp -- RP2350 register addresses used by the Pico 2 board (RISC-V).
 *
 * From the pico-sdk 2.3.0 hardware_regs headers (addressmap.h, sio.h,
 * ticks.h, psm.h, ...) and the RP2350 datasheet; the peripheral set is the
 * Cortex-M pico2 board's (micro-os-plus-iii-cortexm, test/boards/pico2).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cstdint>

namespace rp2350
{
  inline constexpr std::uint32_t CLOCKS_BASE = 0x40010000u;
  inline constexpr std::uint32_t PSM_BASE = 0x40018000u;
  inline constexpr std::uint32_t RESETS_BASE = 0x40020000u;
  inline constexpr std::uint32_t IO_BANK0_BASE = 0x40028000u;
  inline constexpr std::uint32_t PADS_BANK0_BASE = 0x40038000u;
  inline constexpr std::uint32_t XOSC_BASE = 0x40048000u;
  inline constexpr std::uint32_t PLL_SYS_BASE = 0x40050000u;
  inline constexpr std::uint32_t PLL_USB_BASE = 0x40058000u;
  inline constexpr std::uint32_t ADC_BASE = 0x400A0000u;
  inline constexpr std::uint32_t POWMAN_BASE = 0x40100000u; // core VREG
  inline constexpr std::uint32_t UART0_BASE = 0x40070000u;
  inline constexpr std::uint32_t QMI_BASE = 0x400D0000u;
  inline constexpr std::uint32_t WATCHDOG_BASE = 0x400D8000u;
  inline constexpr std::uint32_t TICKS_BASE = 0x40108000u;

  // SIO: core-local single-cycle I/O. On the RISC-V side it also holds the
  // platform timer and the software interrupt (datasheet 3.1.8).
  inline constexpr std::uint32_t SIO_BASE = 0xD0000000u;
  inline constexpr std::uint32_t SIO_CPUID = SIO_BASE + 0x000u;
  inline constexpr std::uint32_t SIO_GPIO_OUT_SET = SIO_BASE + 0x018u;
  inline constexpr std::uint32_t SIO_GPIO_OUT_CLR = SIO_BASE + 0x020u;
  inline constexpr std::uint32_t SIO_GPIO_OUT_XOR = SIO_BASE + 0x028u;
  inline constexpr std::uint32_t SIO_GPIO_OE_SET = SIO_BASE + 0x038u;
  inline constexpr std::uint32_t SIO_GPIO_OUT = SIO_BASE + 0x010u;
  inline constexpr std::uint32_t SIO_FIFO_ST = SIO_BASE + 0x050u;
  inline constexpr std::uint32_t SIO_FIFO_WR = SIO_BASE + 0x054u;
  inline constexpr std::uint32_t SIO_FIFO_RD = SIO_BASE + 0x058u;
  inline constexpr std::uint32_t SIO_SPINLOCK0 = SIO_BASE + 0x100u; // 32 locks
  inline constexpr std::uint32_t SIO_RISCV_SOFTIRQ = SIO_BASE + 0x1A0u;
  inline constexpr std::uint32_t SIO_MTIME_CTRL = SIO_BASE + 0x1A4u;
  inline constexpr std::uint32_t SIO_MTIME = SIO_BASE + 0x1B0u;     // + MTIMEH at +4
  inline constexpr std::uint32_t SIO_MTIMECMP = SIO_BASE + 0x1B8u;  // per core, + H at +4
  inline constexpr std::uint32_t FIFO_ST_VLD = 1u << 0;
  inline constexpr std::uint32_t FIFO_ST_RDY = 1u << 1;

  // TICKS block: the RISC-V platform timer's tick generator.
  inline constexpr std::uint32_t TICKS_RISCV_CTRL = TICKS_BASE + 0x3Cu;
  inline constexpr std::uint32_t TICKS_RISCV_CYCLES = TICKS_BASE + 0x40u;

  inline constexpr std::uint32_t PSM_FRCE_OFF = PSM_BASE + 0x04u;
  inline constexpr std::uint32_t PSM_FRCE_OFF_PROC1 = 1u << 24;

  // Board facts (Raspberry Pi Pico 2).
  inline constexpr std::uint32_t XOSC_HZ = 12000000u;
  // The default clk_sys; the running one is clocks::current_khz().
  inline constexpr std::uint32_t SYS_CLK_HZ = 150000000u;

  // RP2350B potentiometer: ADC channels 0..7 are GPIO40..47; the wiper on
  // GPIO40 = ADC0, the ends on 3V3 and GND.
  inline constexpr std::uint32_t ADC_POT_PIN = 40u;
  inline constexpr std::uint32_t ADC_POT_CHANNEL = 0u;
  // Pins: the Pico 2's by default; a board that shares these sources (the
  // WeAct RP2350B: UART0 on GPIO12/13) sets them in its board.cmake.
#if !defined(PICO2_LED_PIN)
#define PICO2_LED_PIN 25
#endif
#if !defined(PICO2_UART_TX_PIN)
#define PICO2_UART_TX_PIN 0
#endif
#if !defined(PICO2_UART_RX_PIN)
#define PICO2_UART_RX_PIN 1
#endif
  inline constexpr std::uint32_t LED_PIN = PICO2_LED_PIN;
  inline constexpr std::uint32_t UART_TX_PIN = PICO2_UART_TX_PIN; // function 2
  inline constexpr std::uint32_t UART_RX_PIN = PICO2_UART_RX_PIN; // function 2
  // SIO hardware spinlock used to serialise the console between the harts
  // (the kernel lock is an AMO and uses none of them).
  inline constexpr std::uint32_t UART_SPINLOCK = SIO_SPINLOCK0 + 4u * 1u;

  inline volatile std::uint32_t&
  reg (std::uint32_t addr)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (addr);
  }

  inline void
  unreset_block (std::uint32_t mask)
  {
    reg (RESETS_BASE + 0x00u) &= ~mask;
    while ((reg (RESETS_BASE + 0x08u) & mask) != mask)
      {
      }
  }

  // This core's index, 0 or 1 (SIO CPUID; the same as mhartid here).
  inline unsigned
  cpu_id (void)
  {
    return static_cast<unsigned> (reg (SIO_CPUID));
  }

  // h3.unblock (pico-sdk hardware/hazard3/instructions.h): wake the other
  // core from h3.block -- the RISC-V counterpart of Arm's SEV.
  inline void
  h3_unblock (void)
  {
    __asm__ volatile("slt x0, x0, x1" ::: "memory");
  }
} // namespace rp2350
