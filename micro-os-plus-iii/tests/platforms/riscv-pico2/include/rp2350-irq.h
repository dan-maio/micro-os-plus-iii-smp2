/*
 * rp2350-irq.h -- the RP2350 external interrupts, by number (pico-sdk 2.3.0
 * hardware/regs/intctrl.h), with the CMSIS-style names the Cortex-M Pico 2
 * code uses (UART0_IRQn, USBCTRL_IRQn, SIO_IRQ_FIFO_IRQn, ...). On the Hazard3
 * cores they are Xh3irq external interrupts (src/irq.cpp). Generated from
 * intctrl.h.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cmsis_device.h>

enum : IRQn_Type
{
  TIMER0_IRQ_0_IRQn = 0,
  TIMER0_IRQ_1_IRQn = 1,
  TIMER0_IRQ_2_IRQn = 2,
  TIMER0_IRQ_3_IRQn = 3,
  TIMER1_IRQ_0_IRQn = 4,
  TIMER1_IRQ_1_IRQn = 5,
  TIMER1_IRQ_2_IRQn = 6,
  TIMER1_IRQ_3_IRQn = 7,
  PWM_IRQ_WRAP_0_IRQn = 8,
  PWM_IRQ_WRAP_1_IRQn = 9,
  DMA_IRQ_0_IRQn = 10,
  DMA_IRQ_1_IRQn = 11,
  DMA_IRQ_2_IRQn = 12,
  DMA_IRQ_3_IRQn = 13,
  USBCTRL_IRQn = 14,
  PIO0_IRQ_0_IRQn = 15,
  PIO0_IRQ_1_IRQn = 16,
  PIO1_IRQ_0_IRQn = 17,
  PIO1_IRQ_1_IRQn = 18,
  PIO2_IRQ_0_IRQn = 19,
  PIO2_IRQ_1_IRQn = 20,
  IO_IRQ_BANK0_IRQn = 21,
  IO_IRQ_BANK0_NS_IRQn = 22,
  IO_IRQ_QSPI_IRQn = 23,
  IO_IRQ_QSPI_NS_IRQn = 24,
  SIO_IRQ_FIFO_IRQn = 25,
  SIO_IRQ_BELL_IRQn = 26,
  SIO_IRQ_FIFO_NS_IRQn = 27,
  SIO_IRQ_BELL_NS_IRQn = 28,
  SIO_IRQ_MTIMECMP_IRQn = 29,
  CLOCKS_IRQn = 30,
  SPI0_IRQn = 31,
  SPI1_IRQn = 32,
  UART0_IRQn = 33,
  UART1_IRQn = 34,
  ADC_IRQ_FIFO_IRQn = 35,
  I2C0_IRQn = 36,
  I2C1_IRQn = 37,
  OTP_IRQn = 38,
  TRNG_IRQn = 39,
  PROC0_IRQ_CTI_IRQn = 40,
  PROC1_IRQ_CTI_IRQn = 41,
  PLL_SYS_IRQn = 42,
  PLL_USB_IRQn = 43,
  POWMAN_IRQ_POW_IRQn = 44,
  POWMAN_IRQ_TIMER_IRQn = 45,
  SPARE_IRQ_0_IRQn = 46,
  SPARE_IRQ_1_IRQn = 47,
  SPARE_IRQ_2_IRQn = 48,
  SPARE_IRQ_3_IRQn = 49,
  SPARE_IRQ_4_IRQn = 50,
  SPARE_IRQ_5_IRQn = 51,
};

#define RP2350_IRQ_COUNT 52
