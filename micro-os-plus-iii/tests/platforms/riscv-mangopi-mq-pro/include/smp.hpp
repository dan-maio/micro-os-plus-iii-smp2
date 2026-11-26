/*
 * smp.hpp -- the memory-map facts of the board contract.
 *
 * This board has one hart driven by µOS++ (the D1's single C906), so only
 * the RAM window is stated.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cmsis-plus/rtos/port/os-decls.h>

// The first 512 MiB of DDR; both the 512 MiB and 1 GiB boards have it.
#define PORT_RAM_BASE 0x40000000ULL
#define PORT_RAM_END 0x60000000ULL
