/*
 * secondary.S -- core 1's first instructions on the RP2350 (RISC-V).
 *
 * The boot ROM's launch handshake set core 1's mtvec and sp and jumps here
 * with no gp (the pico-sdk's core1_trampoline loads it too). Interrupts
 * off, gp and the trap vector set, then the entry multicore::launch_core1()
 * was asked for (riscv_secondary_c_entry, multicore.cpp).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

	.section .text.secondary, "ax"
	.p2align 2
	.globl	riscv_secondary_trampoline
	.type	riscv_secondary_trampoline, @function
riscv_secondary_trampoline:
	csrci	mstatus, 8
	csrw	mie, zero
	.option push
	.option norelax
	la	gp, __global_pointer$
	.option pop
	la	t0, riscv_trap_entry
	csrw	mtvec, t0
	mv	tp, zero
	fence.i
	la	t0, riscv_secondary_c_entry
	lw	t0, 0(t0)
	jr	t0
	.size	riscv_secondary_trampoline, . - riscv_secondary_trampoline
