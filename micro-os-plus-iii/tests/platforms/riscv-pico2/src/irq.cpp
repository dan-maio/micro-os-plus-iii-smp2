/*
 * irq.cpp -- the RP2350 external interrupts on Hazard3 (Xh3irq), as the
 * NVIC the Cortex-M Pico 2 code calls.
 *
 * Xh3irq (RP2350 datasheet, RVCSR): per core, a 512-bit array of enables
 * (meiea 0xbe0), pendings (meipa 0xbe1), forces (meifa 0xbe2) and 4-bit
 * priorities (meipra 0xbe3), each accessed through a 16-bit window selected
 * by the low bits of the written value; meinext (0xbe4) names the highest-
 * priority pending and enabled IRQ, bit 31 set when there is none. Reading
 * meinext also clears that IRQ's force bit.
 *
 * Mapping:
 *   NVIC_EnableIRQ / DisableIRQ      meiea set / clear (this core) + mie.MEIE
 *   NVIC_SetPendingIRQ / Clear...    meifa set / clear (software-forced)
 *   NVIC_SetPriority (n, p)          meipra = 15 - p (Arm: lower is more
 *                                    urgent; Hazard3: higher is)
 *   the trap's external interrupt    riscv_board_external_interrupt(): loop
 *                                    on meinext, call <NAME>_Handler with
 *                                    MIE set -- higher-priority IRQs PREEMPT
 *
 * Handlers have the Cortex-M vector names (UART0_IRQ_Handler,
 * USBCTRL_IRQ_Handler, SIO_IRQ_FIFO_Handler, ...), weak; an unhandled IRQ
 * is disabled on this core so it cannot storm. External interrupts nest by
 * priority (meicontext.preempt); the timer and software interrupts do not
 * preempt them.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <cmsis_device.h>
#include <riscv-csr.hpp>
#include <rp2350-irq.h>

namespace
{
  inline void
  csr_set_meiea (std::uint32_t v)
  {
    __asm__ volatile("csrs 0xbe0, %0" ::"r"(v) : "memory");
  }
  inline void
  csr_clear_meiea (std::uint32_t v)
  {
    __asm__ volatile("csrc 0xbe0, %0" ::"r"(v) : "memory");
  }
  inline void
  csr_set_meifa (std::uint32_t v)
  {
    __asm__ volatile("csrs 0xbe2, %0" ::"r"(v) : "memory");
  }
  inline void
  csr_clear_meifa (std::uint32_t v)
  {
    __asm__ volatile("csrc 0xbe2, %0" ::"r"(v) : "memory");
  }

  // The window value for IRQ n in a 1-bit-per-IRQ array.
  inline std::uint32_t
  bit_window (unsigned n)
  {
    return (n / 16u) | ((1u << (n % 16u)) << 16);
  }
} // namespace

extern "C"
{
  void
  riscv_rp2350_unhandled_irq (void)
  {
  }

  void TIMER0_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER0_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER0_IRQ_2_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER0_IRQ_3_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER1_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER1_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER1_IRQ_2_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TIMER1_IRQ_3_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PWM_IRQ_WRAP_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PWM_IRQ_WRAP_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void DMA_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void DMA_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void DMA_IRQ_2_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void DMA_IRQ_3_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void USBCTRL_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PIO0_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PIO0_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PIO1_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PIO1_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PIO2_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PIO2_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void IO_IRQ_BANK0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void IO_IRQ_BANK0_NS_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void IO_IRQ_QSPI_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void IO_IRQ_QSPI_NS_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SIO_IRQ_FIFO_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SIO_IRQ_BELL_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SIO_IRQ_FIFO_NS_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SIO_IRQ_BELL_NS_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SIO_IRQ_MTIMECMP_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void CLOCKS_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPI0_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPI1_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void UART0_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void UART1_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void ADC_IRQ_FIFO_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void I2C0_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void I2C1_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void OTP_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void TRNG_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PROC0_IRQ_CTI_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PROC1_IRQ_CTI_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PLL_SYS_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void PLL_USB_IRQ_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void POWMAN_IRQ_POW_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void POWMAN_IRQ_TIMER_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPARE_IRQ_0_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPARE_IRQ_1_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPARE_IRQ_2_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPARE_IRQ_3_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPARE_IRQ_4_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
  void SPARE_IRQ_5_Handler (void) __attribute__ ((weak, alias ("riscv_rp2350_unhandled_irq")));
}

namespace
{
  using handler_t = void (*) (void);
  const handler_t handlers[RP2350_IRQ_COUNT] = {
    TIMER0_IRQ_0_Handler, // 0
    TIMER0_IRQ_1_Handler, // 1
    TIMER0_IRQ_2_Handler, // 2
    TIMER0_IRQ_3_Handler, // 3
    TIMER1_IRQ_0_Handler, // 4
    TIMER1_IRQ_1_Handler, // 5
    TIMER1_IRQ_2_Handler, // 6
    TIMER1_IRQ_3_Handler, // 7
    PWM_IRQ_WRAP_0_Handler, // 8
    PWM_IRQ_WRAP_1_Handler, // 9
    DMA_IRQ_0_Handler, // 10
    DMA_IRQ_1_Handler, // 11
    DMA_IRQ_2_Handler, // 12
    DMA_IRQ_3_Handler, // 13
    USBCTRL_IRQ_Handler, // 14
    PIO0_IRQ_0_Handler, // 15
    PIO0_IRQ_1_Handler, // 16
    PIO1_IRQ_0_Handler, // 17
    PIO1_IRQ_1_Handler, // 18
    PIO2_IRQ_0_Handler, // 19
    PIO2_IRQ_1_Handler, // 20
    IO_IRQ_BANK0_Handler, // 21
    IO_IRQ_BANK0_NS_Handler, // 22
    IO_IRQ_QSPI_Handler, // 23
    IO_IRQ_QSPI_NS_Handler, // 24
    SIO_IRQ_FIFO_Handler, // 25
    SIO_IRQ_BELL_Handler, // 26
    SIO_IRQ_FIFO_NS_Handler, // 27
    SIO_IRQ_BELL_NS_Handler, // 28
    SIO_IRQ_MTIMECMP_Handler, // 29
    CLOCKS_IRQ_Handler, // 30
    SPI0_IRQ_Handler, // 31
    SPI1_IRQ_Handler, // 32
    UART0_IRQ_Handler, // 33
    UART1_IRQ_Handler, // 34
    ADC_IRQ_FIFO_Handler, // 35
    I2C0_IRQ_Handler, // 36
    I2C1_IRQ_Handler, // 37
    OTP_IRQ_Handler, // 38
    TRNG_IRQ_Handler, // 39
    PROC0_IRQ_CTI_Handler, // 40
    PROC1_IRQ_CTI_Handler, // 41
    PLL_SYS_IRQ_Handler, // 42
    PLL_USB_IRQ_Handler, // 43
    POWMAN_IRQ_POW_Handler, // 44
    POWMAN_IRQ_TIMER_Handler, // 45
    SPARE_IRQ_0_Handler, // 46
    SPARE_IRQ_1_Handler, // 47
    SPARE_IRQ_2_Handler, // 48
    SPARE_IRQ_3_Handler, // 49
    SPARE_IRQ_4_Handler, // 50
    SPARE_IRQ_5_Handler, // 51
  };
} // namespace

// The trap's external interrupt, with PREEMPTION by higher-priority IRQs --
// the pico-sdk's isr_riscv_machine_external_irq (crt0_riscv.S), on this
// port's trap frame. trap.S has already saved every register, mepc and
// mstatus in the frame and restores them from it, so mstatus.MIE may be set
// here:
//   - save meicontext with CLEARTS: mie.MTIE/MSIE are cleared (the tick and
//     the software interrupt do not preempt an external handler -- they are
//     lowest, as SysTick and PendSV are on Cortex-M) and their old values
//     kept in the saved word;
//   - loop: read meinext WITH update (meicontext.preempt becomes one above
//     this IRQ's priority, so only higher-priority IRQs can preempt), set
//     MIE, run the handler, clear MIE;
//   - restore meicontext: the preemption level, and MTIE/MSIE as they were.
// A preempting IRQ enters a nested trap (riscv_trap_nesting 2, on the same
// interrupt stack) and comes back here; the thread switch is made only on
// the way out of the outermost trap.
extern "C" void
riscv_board_external_interrupt (void)
{
  std::uint32_t ctx;
  __asm__ volatile("csrrsi %0, 0xbe5, 2" : "=r"(ctx)::"memory"); // CLEARTS
  for (;;)
    {
      std::uint32_t next;
      __asm__ volatile("csrrsi %0, 0xbe4, 1" : "=r"(next)::"memory"); // UPDATE
      if (next & 0x80000000u)
        {
          break; // NOIRQ at this preemption level
        }
      const unsigned n = (next >> 2) & 0x1FFu;
      if (n < RP2350_IRQ_COUNT && handlers[n] != riscv_rp2350_unhandled_irq)
        {
          __asm__ volatile("csrsi mstatus, 8" ::: "memory");
          handlers[n] ();
          __asm__ volatile("csrci mstatus, 8" ::: "memory");
        }
      else
        {
          csr_clear_meiea (bit_window (n)); // nobody handles it: stop it
        }
    }
  __asm__ volatile("csrw 0xbe5, %0" ::"r"(ctx) : "memory");
}

void
NVIC_EnableIRQ (IRQn_Type n)
{
  csr_set_meiea (bit_window (static_cast<unsigned> (n)));
  RISCV_CSR_SET (mie, riscv::csr::MIE_MEIE);
}

void
NVIC_DisableIRQ (IRQn_Type n)
{
  csr_clear_meiea (bit_window (static_cast<unsigned> (n)));
}

void
NVIC_SetPendingIRQ (IRQn_Type n)
{
  csr_set_meifa (bit_window (static_cast<unsigned> (n)));
}

void
NVIC_ClearPendingIRQ (IRQn_Type n)
{
  csr_clear_meifa (bit_window (static_cast<unsigned> (n)));
}

void
NVIC_SetPriority (IRQn_Type n, std::uint32_t priority)
{
  const unsigned irq = static_cast<unsigned> (n);
  const std::uint32_t idx = irq / 4u;
  const unsigned shift = (irq % 4u) * 4u;
  const std::uint32_t hz = 15u - (priority & 15u);
  const riscv::reg_t s = riscv::csr::mstatus_clear_mie ();
  std::uint32_t win;
  // csrrs with only the index set reads the window without changing it.
  __asm__ volatile("csrrs %0, 0xbe3, %1" : "=r"(win) : "r"(idx) : "memory");
  win = (win >> 16) & 0xFFFFu;
  win = (win & ~(0xFu << shift)) | (hz << shift);
  __asm__ volatile("csrw 0xbe3, %0" ::"r"(idx | (win << 16)) : "memory");
  if (s)
    {
      riscv::csr::mstatus_set_mie ();
    }
}
