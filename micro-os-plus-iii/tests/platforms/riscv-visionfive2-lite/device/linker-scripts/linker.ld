/*
 * linker.ld -- StarFive VisionFive 2 Lite (JH-7110S), µOS++ image loaded
 * into DDR by OpenOCD. Everything -- code, data, stacks, heap, the MMU
 * tables -- is in DDR.
 *
 *   0x40000000  OpenSBI (SPL loads it there, FW_TEXT_START; left alone)
 *   0x40200000  this image (OpenSBI's FW_JUMP_ADDR, the U-Boot/bare-metal
 *               load address; U-Boot has relocated itself to the top of
 *               DDR by the time its prompt is up)
 *   0x42200000  end of the 32 MiB window used here
 *
 * The addresses are those of JH-7110S Bare-Metal.md (FW_TEXT_START,
 * FW_JUMP_ADDR, its linker script: ORIGIN 0x40200000, LENGTH 32M).
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x40200000, LENGTH = 32M
}

__irq_stack_size = 16K;
__boot_stack_size = 64K;

SECTIONS
{
  .text : ALIGN(64)
  {
    KEEP(*(.text.entry))
    KEEP(*(.text.park))
    *(.text.trap)
    *(.text .text.*)
    *(.gnu.linkonce.t.*)
  } > RAM

  .rodata : ALIGN(16)
  {
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    *(.gnu.linkonce.r.*)
  } > RAM

  .preinit_array : ALIGN(8)
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } > RAM

  .init_array : ALIGN(8)
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  } > RAM

  .fini_array : ALIGN(8)
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
    KEEP(*(.fini_array))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } > RAM

  .eh_frame : ALIGN(8)
  {
    KEEP(*(.eh_frame))
  } > RAM

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
  } > RAM
  /* Loaded into RAM: load address == run address, startup.S copies nothing. */
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
