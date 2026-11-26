# -----------------------------------------------------------------------------
# semihost-start.gdb -- start the four-hart image with semihosting ON.
#
# Use with jh7110_daplink_semihost.cfg. That config has four INDEPENDENT
# targets and no SMP group, so:
#
#   * GDB is connected only to jh7110.cpu1 (port 3333);
#   * CPUs 1..3 (hart 2..4) are set up and resumed through OpenOCD's
#     `monitor` commands;
#   * OpenOCD services semihosting on every hart independently.
#
# This script runs after Eclipse has loaded Debug/mmu-test-smp-vf2.elf into
# DDR. It halts all harts (again, for Restart), clears the release slots,
# starts the secondaries at _start in M-mode, and runs CPU 0 to os_main.
#
# Semihosting is NOT suppressed here: the app's UART writes are copied to
# OpenOCD's semihosting channel as well, so the console text appears in the
# OpenOCD log/console (and on UART0).
# -----------------------------------------------------------------------------

set pagination off
set confirm off

# The targets are independent; halt each one (the launch's Init Commands do
# this before the load on the first start, this also covers Restart).
monitor targets jh7110.cpu1
monitor halt
monitor targets jh7110.cpu2
monitor halt
monitor targets jh7110.cpu3
monitor halt
monitor targets jh7110.cpu4
monitor halt
monitor targets jh7110.cpu1

# Clear riscv_secondary_boot[]: 4 slots x 3 x 64-bit words = 96 bytes. The
# DDR keeps the previous run's magic; a stale magic would release a hart
# before CPU 0 has prepared its entry and stack.
set $i = 0
while $i < 12
  set {unsigned long long}((char *)&riscv_secondary_boot + $i * 8) = 0
  set $i = $i + 1
end

# CPU 0 (hart 1, GDB's target): M-mode, entry at _start.
monitor reg priv 3
set $pc = &_start

# CPUs 1..3 (harts 2..4): M-mode, entry at _start, resumed. They enter the
# port's riscv_secondary_park loop and wait for multicore::launch_core().
# 0x40200000 is linker.ld's _start; OpenOCD has no symbol table here, so the
# address is literal.
monitor targets jh7110.cpu2
monitor reg priv 3
monitor reg pc 0x40200000
monitor resume

monitor targets jh7110.cpu3
monitor reg priv 3
monitor reg pc 0x40200000
monitor resume

monitor targets jh7110.cpu4
monitor reg priv 3
monitor reg pc 0x40200000
monitor resume

monitor targets jh7110.cpu1

# Run CPU 0 to the kernel's main(), then to the application's os_main().
# Semihosting is live, so console writes are handled by OpenOCD and do not
# stop GDB.
tbreak main
continue

tbreak os_main
continue
delete

echo \n=== mmu-test (SMP, semihosting): stopped at os_main ===\n
echo === GDB controls jh7110.cpu1; harts 2..4 run free. ===\n
echo === Semihosting console is in the OpenOCD log/console. ===\n
