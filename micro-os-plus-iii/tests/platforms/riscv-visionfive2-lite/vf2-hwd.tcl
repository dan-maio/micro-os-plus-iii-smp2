# -----------------------------------------------------------------------------
# One hardware test on the VisionFive 2 Lite (JH-7110S, U74 harts 1..4), the
# board's own steps (the port's former test/boards/visionfive2-lite/hw.sh).
# CTest runs OpenOCD with the probe's configuration and:
#
#   -c init -c "source <image>.facts.tcl" -f vf2-hwd.tcl -c "vf2_hwd_run <elf>"
#
# THE BOARD MUST BE AT THE U-BOOT PROMPT (autoboot stopped): the SPL has
# trained the DDR and U-Boot has set up UART0. The image goes into DDR only
# (0x40200000, linker.ld). The UART is not opened.
#
# The facts (cmake/uos-elf-facts.cmake): uos_elf(entry), uos_elf(riscv_board_park)
# (where harts wait while hart 1 runs the test, src/board.cpp) and, for an SMP
# image, uos_elf(riscv_secondary_boot) and its size: the release slots,
# OS_NCPU of 3 XLEN words (the port's startup.S).
#
# Measured, and why the steps are as they are:
#   - a hart left stopped on the previous run's semihosting exit ebreak
#     (dcsr.cause 1) never halted on its ebreaks in the next run: every hart
#     the image drives is first run briefly on riscv_board_park and stopped by
#     a plain halt request;
#   - a hart halted mid-test keeps its interrupt enables and an overdue CLINT
#     timer: resumed, it took the interrupt at once and ran the new image's
#     trap handler before _start (sometimes leaving the kernel lock held), so
#     mie and mstatus.MIE are cleared before any resume;
#   - the DDR keeps the previous run's release slots: they are cleared before
#     the secondaries start, or a stale magic releases a hart too early.
# -----------------------------------------------------------------------------

proc vf2_hwd_run {elf} {
  global uos_elf
  set entry $uos_elf(entry)
  if {![info exists uos_elf(riscv_board_park)]} {
    echo "VF2-HWD: no riscv_board_park in $elf -- rebuild"
    shutdown error
    return
  }
  set park $uos_elf(riscv_board_park)

  # How many U74 harts the image drives.
  set ncpu 1
  if {[info exists uos_elf(riscv_secondary_boot)]} {
    set slots $uos_elf(riscv_secondary_boot)
    set slots_size [expr {$uos_elf(riscv_secondary_boot,size)}]
    set ncpu [expr {$slots_size / 24}]
  }
  echo "--- VF2: entry $entry, park $park, $ncpu hart(s) ---"

  # All four stopped; the ones the image does not drive stay debug-halted.
  foreach n {2 3 4 1} { targets jh7110.cpu$n; halt }

  targets jh7110.cpu1
  load_image $elf 0 elf

  for {set h 1} {$h <= $ncpu} {incr h} {
    targets jh7110.cpu$h
    reg priv 3
    reg mie 0
    reg mstatus [format 0x%x [expr {[dict get [get_reg mstatus] mstatus] & ~0x8}]]
    reg pc $park
    resume
    sleep 100
    halt
    set pc [dict get [get_reg pc] pc]
    if {$pc != $park} {
      echo "VF2-HWD: PARK FAILED cpu$h: pc=$pc mcause=[dict get [get_reg mcause] mcause]"
    }
    arm semihosting enable
  }

  if {$ncpu > 1} {
    targets jh7110.cpu1
    write_memory $slots 64 [lrepeat [expr {$slots_size / 8}] 0]
    for {set h 2} {$h <= $ncpu} {incr h} {
      targets jh7110.cpu$h
      reg pc $entry
      resume
    }
  }

  targets jh7110.cpu1
  reg pc $entry
  resume
}
