// The Milk-V Duo's support for upstream's "cmsis-os-validator" suite (ARM's
// CMSIS-RTOS v1 validator, tests/sources/cmsis-os-validator and the
// arm-cmsis-rtos-validator xPack). The tests' builder links it into that
// suite only (platforms/<P>/src/<suite>-support.cpp); the board library
// leaves *support.cpp out. It came from the RISC-V port's
// test/milkv-duo/cmsis-os-validator/harness-suite.cpp, whose verdict is now
// the common end of a portable test (board-tests/portable-test-end.cpp).
//
// The validator is Cortex-M code: it raises "NVIC IRQ 0" with
// NVIC_SetPendingIRQ() to call the RTOS from an interrupt. On this port that
// is software interrupt line 0 (src/soft-irq.cpp): pending lines run in the
// MSIP trap, in handler mode. Line 0's handler is the validator's
// DEF_IRQHandler(), which calls the test case's TST_IRQHandler -- what the
// window-watchdog vector does on the STM32 and Mailbox 1 on the Raspberry Pi.

// The validator also probes the Cortex-M DWT cycle counter (0xE0001000,
// RV_WaitFunc.c, StartCortexCycleCounter). Nothing is mapped there on a
// RISC-V hart, so the load faults. As the AArch32 port maps a page that
// reads "no cycle counter", this image answers the same through the port's
// exception hook: a load from the DWT window returns DWT_CTRL.NOCYCCNT (and
// 0 for anything else there), a store is ignored. The validator then skips
// its cycle-count measurements, as on a Cortex-M without a DWT counter.

#include <cstdint>

#include <port_ctx.hpp>

extern "C" void DEF_IRQHandler (void);

namespace
{
  constexpr std::uint64_t DWT_BASE = 0xE0001000u;
  constexpr std::uint64_t DWT_END = 0xE0001010u;
  constexpr std::uint64_t DWT_CTRL_NOCYCCNT = 1u << 25;

  // Emulate the faulting load/store at mepc. Returns false if it is not a
  // plain integer load or store this glue understands.
  bool
  emulate_dwt (ctx_t* frame, std::uint64_t addr, bool is_load)
  {
    const std::uint64_t pc = frame->x[0];
    const std::uint16_t lo = *reinterpret_cast<const std::uint16_t*> (pc);
    unsigned len;
    unsigned rd = 0;

    if ((lo & 3u) == 3u)
      {
        const std::uint32_t insn = *reinterpret_cast<const std::uint32_t*> (pc);
        len = 4;
        const unsigned opcode = insn & 0x7Fu;
        if (is_load && opcode != 0x03u) // LOAD
          {
            return false;
          }
        if (!is_load && opcode != 0x23u) // STORE
          {
            return false;
          }
        rd = (insn >> 7) & 0x1Fu;
      }
    else
      {
        len = 2;
        const unsigned op = lo & 3u;
        const unsigned funct3 = (lo >> 13) & 7u;
        if (op != 0u)
          {
            return false;
          }
        if (is_load && funct3 != 2u && funct3 != 3u) // c.lw / c.ld
          {
            return false;
          }
        if (!is_load && funct3 != 6u && funct3 != 7u) // c.sw / c.sd
          {
            return false;
          }
        rd = 8u + ((lo >> 2) & 7u);
      }

    if (is_load && rd != 0)
      {
        frame->x[rd] = (addr == DWT_BASE) ? DWT_CTRL_NOCYCCNT : 0u;
      }
    frame->x[0] = pc + len;
    return true;
  }
} // namespace

extern "C" bool
riscv_trap_exception_hook (ctx_t* frame, std::uint64_t mcause,
                           std::uint64_t mtval)
{
  // 5 = load access fault, 7 = store/AMO access fault.
  if ((mcause == 5 || mcause == 7) && mtval >= DWT_BASE && mtval < DWT_END)
    {
      return emulate_dwt (frame, mtval, mcause == 5);
    }
  return false;
}

extern "C" void
riscv_soft_irq_handler (unsigned line)
{
  if (line == 0)
    {
      DEF_IRQHandler ();
    }
}
