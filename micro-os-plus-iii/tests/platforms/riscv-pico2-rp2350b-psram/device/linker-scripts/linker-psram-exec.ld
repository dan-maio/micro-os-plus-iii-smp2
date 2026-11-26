/*
 * linker-psram-exec.ld -- WeAct RP2350B, psram-exec: a flash image whose data
 * stays in SRAM, plus two self-contained applications that RUN FROM THE PSRAM
 * (the Cortex-M pico2-pizero board's psram-exec.ld, on linker.ld):
 *
 *   FLASH    0x10000000  the supervisor, and the load images of
 *                        .psram_ram and of both applications
 *   RAM      0x20000000  the supervisor's .data/.bss/stacks/heap, and
 *                        .psram_ram (the QMI init, copied at start-up
 *                        with PICO2_PSRAM_COPY, called by main())
 *   PSRAM_A  0x11000000  application A: header {magic, entry, trap}, code,
 *                        rodata, data; .bss after it
 *   PSRAM_B  0x11400000  application B, the same
 *
 * The supervisor copies each application from its load address (flash)
 * into the PSRAM after psram::init() and calls its entry through a register.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 16M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
  PSRAM_A (rwx) : ORIGIN = 0x11000000, LENGTH = 4M
  PSRAM_B (rwx) : ORIGIN = 0x11400000, LENGTH = 4M
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

  /* The PSRAM init routine: run in SRAM, loaded from flash. */
  .psram_ram : ALIGN(8)
  {
    __psram_ram_start = .;
    KEEP(*(.psram_ram))
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

/* The two applications. The header is first (the supervisor reads entry and
 * trap from it); 64-byte alignment keeps the trap handler legal for mtvec. */
SECTIONS
{
  .psram_a : ALIGN(64)
  {
    __psram_a_vma_start = .;
    KEEP(*(.psram_a_vectors))
    *(.psram_a_text*)
    *(.psram_a_rodata*)
    . = ALIGN(4);
    *(.psram_a_data*)
    . = ALIGN(4);
    __psram_a_vma_end = .;
  } > PSRAM_A AT > FLASH
  __psram_a_lma_start = LOADADDR(.psram_a);

  .psram_a_bss (NOLOAD) : ALIGN(4)
  {
    __psram_a_bss_start = .;
    *(.psram_a_bss*)
    . = ALIGN(4);
    __psram_a_bss_end = .;
  } > PSRAM_A

  .psram_b : ALIGN(64)
  {
    __psram_b_vma_start = .;
    KEEP(*(.psram_b_vectors))
    *(.psram_b_text*)
    *(.psram_b_rodata*)
    . = ALIGN(4);
    *(.psram_b_data*)
    . = ALIGN(4);
    __psram_b_vma_end = .;
  } > PSRAM_B AT > FLASH
  __psram_b_lma_start = LOADADDR(.psram_b);

  .psram_b_bss (NOLOAD) : ALIGN(4)
  {
    __psram_b_bss_start = .;
    *(.psram_b_bss*)
    . = ALIGN(4);
    __psram_b_bss_end = .;
  } > PSRAM_B
}
