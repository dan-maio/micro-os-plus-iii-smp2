/*
 * linker-ram.ld -- Raspberry Pi Pico 2 (RP2350), RISC-V image run from SRAM
 * (the `-ram` tests): nothing goes to flash.
 *
 * hw.sh loads the image into the 512 KiB of striped SRAM over the debugger
 * and starts core 0 at _start; core 1 stays in the boot ROM until the test
 * launches it. Everything loadable is in RAM, so .data needs no copy
 * (__data_load == __data_start). The IMAGE_DEF block is not needed (the boot
 * ROM does not boot this image) and is left out. OpenOCD's work area for
 * verify_image is put inside the NOLOAD heap (hw.sh), never over the image.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

__irq_stack_size = 8K;
__boot_stack_size = 8K;

SECTIONS
{
  /DISCARD/ : { *(.embedded_block) }

  .text : ALIGN(4)
  {
    . = ALIGN(64);
    KEEP(*(.text.entry))
    *(.text.trap)
    *(.text.secondary)
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
