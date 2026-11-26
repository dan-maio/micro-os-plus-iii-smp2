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

#ifndef CMSIS_PLUS_OS_BARE_METAL_H_
#define CMSIS_PLUS_OS_BARE_METAL_H_

// ----------------------------------------------------------------------------
// OS_BARE_METAL: the code runs with no operating system underneath, so the
// framework supplies what an OS would (malloc, exit/atexit, _sbrk, the C++
// runtime hooks, the start-up and the free-store set-up, the semihosting
// system calls, the trace back-ends).
//
// It is derived from what the compiler itself predefines; nothing has to be
// defined on the command line, and no target has to claim to be another:
//
//   Arm 32-bit    the EABI compilers predefine __ARM_EABI__ (as before);
//   RISC-V        riscv-none-elf-gcc predefines __riscv and no OS macro;
//   AArch64       aarch64-none-elf-gcc predefines __aarch64__ and no OS macro.
//
// A RISC-V or AArch64 compiler for an OS predefines that OS (__linux__ on
// Linux; Apple's and Microsoft's compilers __APPLE__ and _WIN32), and the
// synthetic POSIX port runs there, so those are not bare metal.
//
// An application may define OS_BARE_METAL itself to force the choice.

#if !defined(OS_BARE_METAL)

#if defined(__ARM_EABI__)
#define OS_BARE_METAL (1)
#elif (defined(__riscv) || defined(__aarch64__)) && !defined(__linux__) \
    && !defined(__APPLE__) && !defined(_WIN32)
#define OS_BARE_METAL (1)
#endif

#endif // !defined(OS_BARE_METAL)

// ----------------------------------------------------------------------------

#endif // CMSIS_PLUS_OS_BARE_METAL_H_
