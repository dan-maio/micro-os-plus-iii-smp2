/*
 * smp.hpp -- the memory-map facts of the board contract
 * (micro-os-plus::test-support, board-contract.cpp).
 *
 * One hart driven by µOS++; only the RAM window is stated: the first GiB of
 * DDR, the part the MMU maps (riscv-board.hpp).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

#include <cmsis-plus/rtos/port/os-decls.h>

#define PORT_RAM_BASE 0x40000000ULL
#define PORT_RAM_END 0x80000000ULL
