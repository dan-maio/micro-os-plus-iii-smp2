/*
 * smp.hpp -- board-contract memory map: the CH32V307's SRAM as configured
 * (64 KiB, linker.ld).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cmsis-plus/rtos/port/os-decls.h>

#define PORT_RAM_BASE 0x20000000ULL
#define PORT_RAM_END 0x20010000ULL
