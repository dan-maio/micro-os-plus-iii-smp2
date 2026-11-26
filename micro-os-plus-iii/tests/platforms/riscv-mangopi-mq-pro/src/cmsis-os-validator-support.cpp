// The MangoPi MQ-Pro's support for upstream's "cmsis-os-validator" suite (ARM's
// CMSIS-RTOS v1 validator, tests/sources/cmsis-os-validator and the
// arm-cmsis-rtos-validator xPack). The tests' builder links it into that
// suite only (platforms/<P>/src/<suite>-support.cpp); the board library
// leaves *support.cpp out.
//
// The validator is Cortex-M code: it raises "NVIC IRQ 0" with
// NVIC_SetPendingIRQ() to call the RTOS from an interrupt. On this port that
// is software interrupt line 0 (the port's src/soft-irq.cpp): pending lines
// run in the MSIP trap, in handler mode. Line 0's handler is the validator's
// DEF_IRQHandler(), which calls the test case's TST_IRQHandler -- what the
// window-watchdog vector does on the STM32 and Mailbox 1 on the Raspberry Pi.
//
// The validator's other Cortex-M code (its DWT cycle-counter probe, its
// ARM_CMSIS_VALIDATOR_IRQHandler) is compiled only under __ARM_EABI__, which
// a RISC-V compiler does not define, so nothing else is needed here.

extern "C" void DEF_IRQHandler (void);

extern "C" void
riscv_soft_irq_handler (unsigned line)
{
  if (line == 0)
    {
      DEF_IRQHandler ();
    }
}
