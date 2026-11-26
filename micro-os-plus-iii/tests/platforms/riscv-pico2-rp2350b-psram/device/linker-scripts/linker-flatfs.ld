/*
 * linker-flatfs.ld -- WeAct RP2350B, the image in the first 1 MiB of flash
 * and a 3 MiB flat file system after it (FLASH_FS, 0x10100000), the layout
 * of the predecessor's pico2-rp2350-flatfs.ld: _fs_base / _fs_size and
 * _flash_fs_begin / _flash_fs_end for the flash tests (smp-test-xip*).
 * Everything else is linker.ld (data in SRAM: a flash write turns XIP off,
 * and with it the PSRAM).
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 1M
  FLASH_FS (r) : ORIGIN = 0x10100000, LENGTH = 3M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}

__irq_stack_size = 8K;
__boot_stack_size = 8K;

SECTIONS
{
  .text : ALIGN(4)
  {
    KEEP(*(.embedded_block))
    . = ALIGN(64);
    KEEP(*(.text.entry))
    *(.text.trap)
    *(.text.secondary)
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

_fs_base = ORIGIN(FLASH_FS);
_fs_size = LENGTH(FLASH_FS);
_flash_fs_begin = ORIGIN(FLASH_FS);
_flash_fs_end = ORIGIN(FLASH_FS) + LENGTH(FLASH_FS);
