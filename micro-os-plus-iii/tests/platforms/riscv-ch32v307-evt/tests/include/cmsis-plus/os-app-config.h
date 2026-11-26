/*
 * os-app-config.h -- the CH32V307-EVT board's say in a harness suite's
 * configuration.
 *
 * The kernel reads os-app-config.h only for the harness suites
 * (OS_USE_OS_APP_CONFIG_H). This directory (test/ch32v307-evt/include) is
 * searched before the suite's, so this file is found first: it hands over
 * to the suite's own file and then adjusts what does not fit the chip, as
 * the Pico 2 board's file does.
 *
 * The CH32V307 has 64 KiB of SRAM (as configured, board linker.ld). The
 * RTOS arena (OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES) is carved from the
 * free store at start-up: rtos-apis asks for 512 KiB. It is capped here,
 * and the FatFs sub-test (a 32 KiB working buffer, RAM disks of ~33 MiB)
 * is left out.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef CH32V307_EVT_OS_APP_CONFIG_H_
#define CH32V307_EVT_OS_APP_CONFIG_H_

#include_next <cmsis-plus/os-app-config.h>

#if defined(OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES)
#undef OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES
#define OS_INTEGER_RTOS_DYNAMIC_MEMORY_SIZE_BYTES (20 * 1024)
#endif

#if !defined(OS_EXCLUDE_RTOS_APIS_FATFS)
#define OS_EXCLUDE_RTOS_APIS_FATFS
#endif

#endif /* CH32V307_EVT_OS_APP_CONFIG_H_ */
