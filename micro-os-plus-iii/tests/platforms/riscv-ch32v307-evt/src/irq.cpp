/*
 * irq.cpp -- the CH32V307's peripheral interrupts on the PFIC, and the
 * NVIC_* names Cortex-M code calls, for a RISCV_PFIC board.
 *
 * The handlers have WCH's names (EVT startup_ch32v30x_D8C.S, PFIC
 * interrupts 16..103; this table is generated from it). Each is weak: a
 * test or a driver defines the one it uses. riscv_board_pfic_interrupt()
 * (called by the port's trap dispatcher, src/trap.cpp) runs it; one left
 * at the default is reported back as unhandled, and the port disables it
 * and reports a fault.
 *
 * NVIC_* -- the Cortex-M numbering: IRQ n is PFIC interrupt 16 + n (IRQ 0 =
 * WWDG). NVIC_SetPriority puts every one at PRIO_KERNEL_HIGH (0x40): such
 * a handler may call the kernel, so the critical-section threshold must
 * mask it (Cortex-M code passes NVIC levels, not PFIC bytes). Enabling a
 * line discards a request left pending while it was off, as the port's
 * software lines do (src/soft-irq.cpp: the CMSIS validator relies on it).
 * NVIC_SetPendingIRQ from a thread that the interrupt can preempt now waits
 * until the handler has run -- on Cortex-M it has run before the next
 * instruction; the PFIC takes it a few instructions after the store.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cstdint>

#include <cmsis_device.h>
#include <pfic.hpp>
#include <riscv-csr.hpp>
#include <riscv-hart.hpp>

namespace
{
  volatile bool g_unhandled = false;
}

extern "C"
{
  void
  ch32_unhandled_irq (void)
  {
    g_unhandled = true;
  }

  void WWDG_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void PVD_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TAMPER_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void RTC_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void FLASH_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void RCC_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI0_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI3_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI4_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel3_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel4_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel5_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel6_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA1_Channel7_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void ADC1_2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USB_HP_CAN1_TX_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USB_LP_CAN1_RX0_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void CAN1_RX1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void CAN1_SCE_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI9_5_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM1_BRK_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM1_UP_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM1_TRG_COM_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM1_CC_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM3_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM4_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void I2C1_EV_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void I2C1_ER_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void I2C2_EV_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void I2C2_ER_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void SPI1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void SPI2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USART1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USART2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USART3_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void EXTI15_10_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void RTCAlarm_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USBWakeUp_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM8_BRK_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM8_UP_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM8_TRG_COM_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM8_CC_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void RNG_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void SDIO_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM5_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void SPI3_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void UART4_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void UART5_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM6_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM7_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel2_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel3_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel4_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel5_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void ETH_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void ETH_WKUP_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void CAN2_TX_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void CAN2_RX0_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void CAN2_RX1_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void CAN2_SCE_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USBFS_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USBHSWakeup_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void USBHS_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DVP_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void UART6_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void UART7_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void UART8_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM9_BRK_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM9_UP_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM9_TRG_COM_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM9_CC_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM10_BRK_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM10_UP_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM10_TRG_COM_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void TIM10_CC_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel6_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel7_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel8_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel9_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel10_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
  void DMA2_Channel11_IRQHandler (void) __attribute__ ((weak, alias ("ch32_unhandled_irq")));
}

namespace
{
  constexpr unsigned kFirst = 16;
  constexpr unsigned kLast = 103;

  void (*const handlers[kLast - kFirst + 1]) (void) = {
    WWDG_IRQHandler,
    PVD_IRQHandler,
    TAMPER_IRQHandler,
    RTC_IRQHandler,
    FLASH_IRQHandler,
    RCC_IRQHandler,
    EXTI0_IRQHandler,
    EXTI1_IRQHandler,
    EXTI2_IRQHandler,
    EXTI3_IRQHandler,
    EXTI4_IRQHandler,
    DMA1_Channel1_IRQHandler,
    DMA1_Channel2_IRQHandler,
    DMA1_Channel3_IRQHandler,
    DMA1_Channel4_IRQHandler,
    DMA1_Channel5_IRQHandler,
    DMA1_Channel6_IRQHandler,
    DMA1_Channel7_IRQHandler,
    ADC1_2_IRQHandler,
    USB_HP_CAN1_TX_IRQHandler,
    USB_LP_CAN1_RX0_IRQHandler,
    CAN1_RX1_IRQHandler,
    CAN1_SCE_IRQHandler,
    EXTI9_5_IRQHandler,
    TIM1_BRK_IRQHandler,
    TIM1_UP_IRQHandler,
    TIM1_TRG_COM_IRQHandler,
    TIM1_CC_IRQHandler,
    TIM2_IRQHandler,
    TIM3_IRQHandler,
    TIM4_IRQHandler,
    I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler,
    I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler,
    SPI1_IRQHandler,
    SPI2_IRQHandler,
    USART1_IRQHandler,
    USART2_IRQHandler,
    USART3_IRQHandler,
    EXTI15_10_IRQHandler,
    RTCAlarm_IRQHandler,
    USBWakeUp_IRQHandler,
    TIM8_BRK_IRQHandler,
    TIM8_UP_IRQHandler,
    TIM8_TRG_COM_IRQHandler,
    TIM8_CC_IRQHandler,
    RNG_IRQHandler,
    nullptr,
    SDIO_IRQHandler,
    TIM5_IRQHandler,
    SPI3_IRQHandler,
    UART4_IRQHandler,
    UART5_IRQHandler,
    TIM6_IRQHandler,
    TIM7_IRQHandler,
    DMA2_Channel1_IRQHandler,
    DMA2_Channel2_IRQHandler,
    DMA2_Channel3_IRQHandler,
    DMA2_Channel4_IRQHandler,
    DMA2_Channel5_IRQHandler,
    ETH_IRQHandler,
    ETH_WKUP_IRQHandler,
    CAN2_TX_IRQHandler,
    CAN2_RX0_IRQHandler,
    CAN2_RX1_IRQHandler,
    CAN2_SCE_IRQHandler,
    USBFS_IRQHandler,
    USBHSWakeup_IRQHandler,
    USBHS_IRQHandler,
    DVP_IRQHandler,
    UART6_IRQHandler,
    UART7_IRQHandler,
    UART8_IRQHandler,
    TIM9_BRK_IRQHandler,
    TIM9_UP_IRQHandler,
    TIM9_TRG_COM_IRQHandler,
    TIM9_CC_IRQHandler,
    TIM10_BRK_IRQHandler,
    TIM10_UP_IRQHandler,
    TIM10_TRG_COM_IRQHandler,
    TIM10_CC_IRQHandler,
    DMA2_Channel6_IRQHandler,
    DMA2_Channel7_IRQHandler,
    DMA2_Channel8_IRQHandler,
    DMA2_Channel9_IRQHandler,
    DMA2_Channel10_IRQHandler,
    DMA2_Channel11_IRQHandler,
  };

  inline unsigned
  pfic_irq (IRQn_Type n)
  {
    return static_cast<unsigned> (n) + kFirst;
  }

  inline bool
  valid (IRQn_Type n)
  {
    return n >= 0 && pfic_irq (n) <= kLast;
  }
} // namespace

extern "C" bool
riscv_board_pfic_interrupt (unsigned irq)
{
  if (irq < kFirst || irq > kLast || handlers[irq - kFirst] == nullptr)
    {
      return false;
    }
  g_unhandled = false;
  handlers[irq - kFirst]();
  return !g_unhandled;
}

void
NVIC_EnableIRQ (IRQn_Type n)
{
  if (!valid (n))
    {
      return;
    }
  pfic::clear_pending (pfic_irq (n));
  pfic::enable (pfic_irq (n));
}

void
NVIC_DisableIRQ (IRQn_Type n)
{
  if (valid (n))
    {
      pfic::disable (pfic_irq (n));
      __asm__ volatile("fence.i" ::: "memory");
    }
}

void
NVIC_SetPendingIRQ (IRQn_Type n)
{
  if (!valid (n))
    {
      return;
    }
  const unsigned irq = pfic_irq (n);
  pfic::set_pending (irq);
  __asm__ volatile("fence.i" ::: "memory");
  const std::uint32_t t = pfic::threshold ();
  const bool enabled
      = (pfic::reg32 (0xE000E000u + 4u * (irq / 32u)) >> (irq % 32u)) & 1u;
  if (!enabled || riscv::in_trap ()
      || (RISCV_CSR_READ (mstatus) & riscv::csr::MSTATUS_MIE) == 0
      || (t != 0u && pfic::PRIO_KERNEL_HIGH >= t))
    {
      return;
    }
  while (pfic::is_pending (irq))
    {
    }
}

void
NVIC_ClearPendingIRQ (IRQn_Type n)
{
  if (valid (n))
    {
      pfic::clear_pending (pfic_irq (n));
    }
}

void
NVIC_SetPriority (IRQn_Type n, std::uint32_t priority)
{
  (void) priority;
  if (valid (n))
    {
      pfic::set_priority (pfic_irq (n), pfic::PRIO_KERNEL_HIGH);
    }
}
