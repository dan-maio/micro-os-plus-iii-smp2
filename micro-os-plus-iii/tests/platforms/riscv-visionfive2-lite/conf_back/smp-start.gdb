# -----------------------------------------------------------------------------
# smp-start.gdb -- start the four-hart image under GDB.
#
# Executed by the Eclipse launch ("Run/Restart Commands") AFTER Eclipse has
# loaded Debug/smp-mat-test-vf2.elf into DDR at 0x40200000. The GDB is
# connected to OpenOCD's single SMP-group server (jh7110_daplink_debug.cfg):
# OpenOCD's hwthread pseudo-RTOS presents the four U74 harts as GDB threads.
# The thread id is coreid + 1, so the harts are threads 2, 3, 4 and 5; the
# script below does not depend on those numbers.
#
# The board must be halted at the U-Boot prompt; no reset is issued here.
# -----------------------------------------------------------------------------

set pagination off
set confirm off

# All harts are halted by the launch's initialization command ('monitor
# halt'). Clear riscv_secondary_boot[]: 4 slots x 3 x 64-bit words = 96
# bytes. The DDR keeps the previous run's magic, and a stale magic would
# release a hart before CPU 0 has prepared its entry and stack. CPU 0 also
# zeroes .bss, but a secondary may read its slot before that happens.
set $i = 0
while $i < 12
  set {unsigned long long}((char *)&riscv_secondary_boot + $i * 8) = 0
  set $i = $i + 1
end

# Every hart starts at _start in M-mode (the board is at U-Boot, which runs
# in S-mode). Harts 2..4 take the port's riscv_secondary_park path and wait
# for multicore::launch_core(); hart 1 boots the kernel.
#
# The `reg` command acts on OpenOCD's current target, so name each target
# explicitly. This is independent of the GDB thread numbering.
monitor targets jh7110.cpu1
monitor reg priv 3
monitor targets jh7110.cpu2
monitor reg priv 3
monitor targets jh7110.cpu3
monitor reg priv 3
monitor targets jh7110.cpu4
monitor reg priv 3
monitor targets jh7110.cpu1

# Set every hart's PC to _start. 'thread apply all' covers whatever thread
# ids the hwthread RTOS assigned (2..5 here).
thread apply all set $pc = &_start

# The kernel's start-up prints (Hardware initialised, Main stack,
# os_startup_initialize_free_store, ...) happen BEFORE main(). If the image
# is built with SEMIHOST=1, each of those UART writes is also a semihosting
# ebreak; with OpenOCD semihosting disabled, GDB would stop on them before
# this script could reach main(). So set the flag at the board's
# os_startup_initialize_hardware() hook: the kernel has already zeroed .bss
# by then, and the first trace line has not been written yet.
#
# Do not hard-code a GDB thread id here: OpenOCD numbers hwthreads as
# coreid + 1 (2..5 on this board), but a different OpenOCD build may number
# them differently. The breakpoint is installed on every hart of the SMP
# group anyway.
tbreak os_startup_initialize_hardware
continue
set var riscv_semihosting_absent = 1
delete
printf "riscv_semihosting_absent = %d\n", riscv_semihosting_absent

# Now run to the application's entry, so the session opens at a convenient
# place with the scheduler started and the three secondary idle threads
# already parked. Remove this block if you want to debug start-up itself.
tbreak os_main
continue
delete

echo \n=== mmu-test (SMP): stopped at os_main, 4 U74 harts under GDB ===\n
echo === Console: UART0 115200 8N1. 'info threads' shows the harts. ===\n
