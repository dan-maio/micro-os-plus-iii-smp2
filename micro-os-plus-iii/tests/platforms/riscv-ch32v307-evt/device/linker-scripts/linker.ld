/*
 * linker.ld -- WCH CH32V307 (CH32V307VCT6), image in user flash.
 *
 * The code runs from user flash at its 0x00000000 alias, where the core
 * starts after reset (WCH's own link address, EVT Ld/Link.ld), and the WCH
 * OpenOCD's flash bank is there too (hw.sh).
 *
 * Sizes as the chip is configured: FLASH_OBR = 0x03fffefc (measured),
 * SRAM_CODE_MODE bits [9:8] = 10b = "CODE-256KB + RAM-64KB" (RM,
 * FLASH_OBR); the WCH OpenOCD reports the same ("ROM 256 kbytes RAM 64
 * kbytes"). ESIG_FLACAP's 0x0120 (288 KiB) is the whole flash, not the
 * zero-wait code area. .data is copied to SRAM by startup.S (__data_load).
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

__irq_stack_size = 2K;
__boot_stack_size = 2K;

SECTIONS
{
  .text : ALIGN(4)
  {
    KEEP(*(.text.entry))
    *(.text.trap)
    *(.text .text.*)
    *(.gnu.linkonce.t.*)
  } > FLASH

  .rodata : ALIGN(16)
  {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    *(.gnu.linkonce.r.*)
  } > FLASH

  .preinit_array : ALIGN(8)
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > FLASH

  .init_array : ALIGN(8)
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > FLASH

  .fini_array : ALIGN(8)
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > FLASH

  .eh_frame : ALIGN(8)
  {
    KEEP(*(.eh_frame))
  } > FLASH

  .gcc_except_table : ALIGN(4)
  {
    *(.gcc_except_table .gcc_except_table.*)
    /* Pad to .data's alignment, so its initial values follow the code
     * without a gap (hw.sh compares each segment's file bytes only, so a
     * gap -- or this padding alone, which the linker makes NOBITS -- is
     * harmless either way). */
    . = ALIGN(16);
  } > FLASH

  .data : ALIGN(16)
  {
    __data_start = .;
    *(.data .data.*)
    *(.gnu.linkonce.d.*)
    . = ALIGN(8);
    PROVIDE(__global_pointer$ = . + 0x800);
    *(.sdata .sdata.*)
    *(.gnu.linkonce.s.*)
    . = ALIGN(8);
    __data_end = .;
  } > RAM AT > FLASH
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(16)
  {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.gnu.linkonce.sb.*)
    *(.bss .bss.*)
    *(.gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > RAM

  .irq_stack (NOLOAD) : ALIGN(16)
  {
    __irq_stack_bottom = .;
    . += __irq_stack_size;
    __irq_stack_top = .;
  } > RAM

  .boot_stack (NOLOAD) : ALIGN(16)
  {
    __stack_bottom = .;
    . += __boot_stack_size;
    __stack_top = .;
  } > RAM

  .heap (NOLOAD) : ALIGN(16)
  {
    _Heap_Begin = .;
  } > RAM
  _Heap_Limit = ORIGIN(RAM) + LENGTH(RAM);
  end = _Heap_Begin;
}
