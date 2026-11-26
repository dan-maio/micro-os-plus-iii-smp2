/*
 * This file is part of the µOS++ project (https://micro-os-plus.github.io/).
 * Copyright (c) 2026 Liviu Ionescu. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose is hereby granted, under the terms of the MIT license.
 *
 * If a copy of the license was not distributed with this file, it can be
 * obtained from https://opensource.org/licenses/mit.
 */

// ----------------------------------------------------------------------------
// The Raspberry Pi Pico 2 (RP2350, 2x Hazard3 RV32) harness platform.
//
// The suites' os-app-config.h include this file. Nothing Cortex-M is
// available here: interrupts are mstatus.MIE, and the CMSIS-RTOS validator's
// "NVIC IRQ 0" is the port's software-interrupt line (see the port's
// cmsis_device.h).
// ----------------------------------------------------------------------------

#ifndef PLATFORM_RISCV_PICO2_PLATFORM_H_
#define PLATFORM_RISCV_PICO2_PLATFORM_H_

#if defined(__riscv) && !defined(__disable_irq)
static inline void
__disable_irq (void)
{
  __asm__ volatile("csrci mstatus, 8" ::: "memory");
}
static inline void
__enable_irq (void)
{
  __asm__ volatile("csrsi mstatus, 8" ::: "memory");
}
#endif

#endif /* PLATFORM_RISCV_PICO2_PLATFORM_H_ */
