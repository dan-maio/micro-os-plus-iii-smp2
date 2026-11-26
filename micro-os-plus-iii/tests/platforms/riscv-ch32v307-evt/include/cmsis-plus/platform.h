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
// The WCH CH32V307V-EVT-R1 (CH32V307, one QingKe V4F core) harness platform.
//
// The suites' os-app-config.h include this file. Nothing Cortex-M is
// available here. The kernel's critical sections are the PFIC threshold
// (the port's RISCV_PFIC path); __disable_irq/__enable_irq, which Cortex-M
// test code calls directly, are mstatus.MIE -- every interrupt, as PRIMASK.
// The CMSIS-RTOS validator's "NVIC IRQ 0" is PFIC interrupt 16 (the board's
// NVIC_*, src/irq.cpp).
// ----------------------------------------------------------------------------

#ifndef PLATFORM_RISCV_CH32V307_EVT_PLATFORM_H_
#define PLATFORM_RISCV_CH32V307_EVT_PLATFORM_H_

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

#endif /* PLATFORM_RISCV_CH32V307_EVT_PLATFORM_H_ */
