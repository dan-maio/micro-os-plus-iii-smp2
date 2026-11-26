/*
 * linker.ld -- MangoPi MQ-Pro (Allwinner D1), µOS++ image loaded into DRAM
 * by xfel.  Everything -- code, data, stacks, heap, MMU tables -- is in DDR.
 *
 *   0x40000000  this image (xfel ddr d1; xfel write 0x40000000 <bin>;
 *               xfel exec 0x40000000)
 *   0x60000000  end of the 512 MiB window used here; the 1 GiB boards simply
 *               have more RAM above it
 *
 * xfel writes a flat binary, so the image must be contiguous from the load
 * address; .bss, the stacks and the heap are NOLOAD and are set up by the
 * port's startup.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 0x20000000
}

__irq_stack_size = 16K;
__boot_stack_size = 64K;

SECTIONS
{
  .text : ALIGN(64)
  {
    KEEP(*(.text.entry))
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
  /* The image is loaded into RAM: load address == run address, so
     startup.S copies nothing. */
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
