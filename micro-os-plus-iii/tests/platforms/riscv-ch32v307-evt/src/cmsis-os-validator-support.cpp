// The WCH CH32V307V-EVT's support for upstream's "cmsis-os-validator" suite
// (ARM's CMSIS-RTOS v1 validator, tests/sources/cmsis-os-validator and the
// arm-cmsis-rtos-validator xPack). The tests' builder links it into that
// suite only (platforms/<P>/src/<suite>-support.cpp); the board library
// leaves *support.cpp out.
//
// The validator is Cortex-M code: it raises "NVIC IRQ 0" with
// NVIC_SetPendingIRQ() to call the RTOS from an interrupt. On the QingKe V4F
// the NVIC is the PFIC (the port's PFIC path, RISCV_PFIC), whose IRQ 0 here
// is the window watchdog's, as on the STM32 boards: its handler is the
// validator's DEF_IRQHandler(), which calls the test case's TST_IRQHandler.
//
// The validator's other Cortex-M code (its DWT cycle-counter probe, its
// ARM_CMSIS_VALIDATOR_IRQHandler) is compiled only under __ARM_EABI__, which
// a RISC-V compiler does not define, so nothing else is needed here.

extern "C" void DEF_IRQHandler (void);

extern "C" void
WWDG_IRQHandler (void)
{
  DEF_IRQHandler ();
}
