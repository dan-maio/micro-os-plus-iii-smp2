/*
 * smp.hpp -- board-contract memory map: the RP2350's main SRAM.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cmsis-plus/rtos/port/os-decls.h>

#define PORT_RAM_BASE 0x20000000ULL
#define PORT_RAM_END 0x20082000ULL
