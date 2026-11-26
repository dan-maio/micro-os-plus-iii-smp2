/*
 * linker-psram.ld -- WeAct RP2350B (16 MB flash, 8 MB PSRAM), RISC-V image in
 * flash with its data in the PSRAM. The layout of the Cortex-M board's
 * pico2-rp2350b-psram.ld:
 *
 *   FLASH  0x10000000  code, read-only data, the .data image, .psram_ram's
 *                      image
 *   PSRAM  0x11000000  .data, .bss, the heap (thread stacks come from it)
 *   RAM    0x20000000  .psram_ram (the QMI init, run before the PSRAM
 *                      exists, and the AMO targets), the boot and IRQ
 *                      stacks, .noinit
 *
 * startup.S calls riscv_board_initialize_memory() (src/psram.cpp) before it
 * copies .data, so the PSRAM is up before anything is written to it.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
  PSRAM (rwx) : ORIGIN = 0x11000000, LENGTH = 8M
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

  /* What must be in SRAM before and after the PSRAM exists, loaded from
   * flash and copied by riscv_board_initialize_memory() before .data: the
   * PSRAM init routine, and the objects the port changes with AMOs (the
   * PSRAM window faults on them; include/riscv-amo.hpp). The zero-initialized
   * ones are copied as zeros from the image. */
  .psram_ram : ALIGN(8)
  {
    __psram_ram_start = .;
    KEEP(*(.psram_ram))
    . = ALIGN(8);
    /* The flash writer (src/flash_rp2350.cpp): runs while XIP is off. */
    KEEP(*(.data.ramfunc .data.ramfunc.*))
    . = ALIGN(8);
    *(.data.amo)
    *(.bss.amo)
    . = ALIGN(4);
    __psram_ram_end = .;
  } > RAM AT > FLASH
  __psram_ram_load = LOADADDR(.psram_ram);

  .noinit (NOLOAD) : ALIGN(4)
  {
    *(.noinit .noinit.*)
    . = ALIGN(4);
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
  } > PSRAM AT > FLASH
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
  } > PSRAM

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
  } > PSRAM
  _Heap_Limit = ORIGIN(PSRAM) + LENGTH(PSRAM);
  end = _Heap_Begin;
}
