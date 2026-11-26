// The RISC-V Pico 2's support for upstream's "cmsis-os-validator" suite (ARM's
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

// On the Pico 2 the NVIC is the RP2350's (Xh3irq, the board's src/irq.cpp):
// the validator's IRQ 0 is TIMER0_IRQ_0, made pending by NVIC_SetPendingIRQ
// through the force array (meifa); its handler is the same DEF_IRQHandler.
extern "C" void
TIMER0_IRQ_0_Handler (void)
{
  DEF_IRQHandler ();
}
