/*
 * os-app-config.h -- the Pico 2 (RISC-V) board's say in a harness suite's
 * configuration.
 *
 * The kernel reads os-app-config.h only for the harness suites
 * (OS_USE_OS_APP_CONFIG_H). This directory (test/pico2/include) is searched
 * before the suite's, so this file is found first: it hands over to the
 * suite's own file and then adjusts what does not fit the RP2350 -- as the
 * Cortex-M Pico 2 board's os-app-config.h does.
 *
 * Measured: rtos-apis asks for a 512 KiB RTOS arena
 * (OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES), carved from the free store at
 * start-up; the Pico 2 image's free store is 468992 bytes, so it threw
 * bad_alloc before the scheduler started. On hardware the arena is capped at
 * 96 KiB (FatFs's 32 KiB working buffer plus the RTOS objects; the Cortex-M
 * board's value), and the FatFs sub-test, whose RAM disks are ~33 MiB, is
 * left out. QEMU (128 MiB) keeps the suite's values.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef PICO2_RISCV_OS_APP_CONFIG_H_
#define PICO2_RISCV_OS_APP_CONFIG_H_

#include_next <cmsis-plus/os-app-config.h>

#if defined(HW_BUILD) && defined(OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES)
#undef OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES
#define OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES (96 * 1024)
#endif

#if defined(HW_BUILD) && !defined(OS_EXCLUDE_RTOS_APIS_FATFS)
#define OS_EXCLUDE_RTOS_APIS_FATFS
#endif

#endif /* PICO2_RISCV_OS_APP_CONFIG_H_ */
