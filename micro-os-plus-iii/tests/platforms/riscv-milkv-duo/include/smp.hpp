/*
 * smp.hpp -- the memory-map facts of the board contract
 * (micro-os-plus::test-support, board-contract.cpp).
 *
 * The name is the contract's: the AArch ports keep their SMP helpers here.
 * This board has one hart driven by µOS++, so only the RAM window is stated.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cmsis-plus/rtos/port/os-decls.h>

// The Duo's 64 MiB of DDR2.
#define PORT_RAM_BASE 0x80000000ULL
#define PORT_RAM_END 0x84000000ULL
