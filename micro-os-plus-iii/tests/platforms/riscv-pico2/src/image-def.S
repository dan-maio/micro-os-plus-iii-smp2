/*
 * image-def.S -- the RP2350 boot ROM's IMAGE_DEF block for a RISC-V image.
 *
 * The pico-sdk's embedded_start_block format (pico_crt0): IMAGE_TYPE = EXE,
 * CPU RISC-V, chip RP2350, and ENTRY_POINT {entry, sp}. The boot ROM scans
 * the first 4 KiB of flash for it (the linker script places it first); on an
 * Arm boot it switches both cores to Hazard3 and reboots into the image.
 * The constants are the SDK's (boot/picobin.h), written out here.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#define PICOBIN_BLOCK_MARKER_START 0xffffded3
#define PICOBIN_BLOCK_MARKER_END 0xab123579
#define PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE 0x42
#define PICOBIN_BLOCK_ITEM_1BS_ENTRY_POINT 0x44
#define PICOBIN_BLOCK_ITEM_2BS_LAST 0xff
/* EXE (1 << 0) | CPU RISC-V (1 << 8) | CHIP RP2350 (1 << 12) */
#define IMAGE_TYPE_EXE_RISCV_RP2350 0x1101

	.section .embedded_block, "a"
	.p2align 2
embedded_block:
	.word	PICOBIN_BLOCK_MARKER_START
	.byte	PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE
	.byte	0x1
	.hword	IMAGE_TYPE_EXE_RISCV_RP2350
	.byte	PICOBIN_BLOCK_ITEM_1BS_ENTRY_POINT
	.byte	0x3
	.byte	0
	.byte	0
	.word	_start
	.word	__stack_top
	.byte	PICOBIN_BLOCK_ITEM_2BS_LAST
	.hword	(embedded_block_end - embedded_block - 16) / 4
	.byte	0
	.word	0
	.word	PICOBIN_BLOCK_MARKER_END
embedded_block_end:
