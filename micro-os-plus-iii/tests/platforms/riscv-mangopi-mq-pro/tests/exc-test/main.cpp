/*
 * exc-test -- does exception ENTRY work, and does the port survive it?
 *
 * The RISC-V counterpart of the RP2350's exc-test. That one asks whether a
 * fault reaches its handler at all; on RISC-V every exception and interrupt
 * shares one entry (trap.S), so the question here is sharper: an exception
 * taken in the middle of a running thread must save and restore that
 * thread's full frame -- integer AND floating-point registers -- and leave
 * the scheduler, the tick and the context switch exactly as they were.
 *
 * Provoked from a thread, with the scheduler running:
 *   illegal instruction   (mcause 2)   the canonical 32-bit `unimp`
 *                                      (csrrw x0, cycle, x0: a write to a
 *                                      read-only CSR). Not `.word 0`: on RVC
 *                                      that is TWO defined-illegal 16-bit
 *                                      instructions, and traps twice.
 *   ecall from M-mode     (mcause 11)
 *   load access fault     (mcause 5)   a load from an unmapped hole
 *
 * The test claims them through riscv_trap_exception_hook() (src/trap.cpp),
 * steps over the instruction and counts. Registers live across each trap
 * are checked afterwards, and a second thread keeps running throughout.
 *
 * The access-fault address is a board fact: QEMU virt has nothing at
 * 0x00000000-0x00000FFF; on the CV1800B the first page is the boot ROM's
 * neighbourhood, so the hardware image skips that part (see below).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <cmsis-plus/rtos/os.h>

#include <cstdint>
#include <cstdio>

#include <board.hpp>
#include <hw_result.hpp>
#include <port_ctx.hpp>
#include <riscv-csr.hpp>
#include <uart.hpp>

using namespace os::rtos;

namespace
{
  volatile unsigned n_illegal = 0;
  volatile unsigned n_ecall = 0;
  volatile unsigned n_access = 0;
  volatile unsigned n_other = 0;
  volatile unsigned ticker_runs = 0;
  volatile bool stop = false;

  // Length of the instruction at pc: 2 if compressed (low bits != 11).
  unsigned
  insn_len (std::uint64_t pc)
  {
    const std::uint16_t lo = *reinterpret_cast<const std::uint16_t*> (pc);
    return ((lo & 3u) == 3u) ? 4u : 2u;
  }

  void*
  ticker (void*)
  {
    while (!stop)
      {
        ticker_runs = ticker_runs + 1;
        sysclock.sleep_for (1);
      }
    return nullptr;
  }

  thread::stack::element_t ticker_stack[1024];
} // namespace

extern "C" bool
riscv_trap_exception_hook (ctx_t* frame, std::uint64_t mcause,
                           std::uint64_t mtval)
{
  (void) mtval;
  switch (mcause)
    {
    case 2: // illegal instruction
      n_illegal = n_illegal + 1;
      break;
    case 11: // environment call from M-mode
      n_ecall = n_ecall + 1;
      break;
    case 5: // load access fault
      n_access = n_access + 1;
      break;
    default:
      n_other = n_other + 1;
      return false;
    }
  frame->x[0] += insn_len (frame->x[0]);
  return true;
}

int
os_main (int, char*[])
{
  uart::init (board::UART_BAUD);
  uart::write ("\nexc-test on " PORT_BANNER_LONG "\n");

  thread::attributes attr = thread::initializer;
  attr.th_stack_address = ticker_stack;
  attr.th_stack_size_bytes = sizeof (ticker_stack);
  thread th{ "ticker", ticker, nullptr, attr };

  constexpr unsigned ROUNDS = 50;
  bool regs_ok = true;

  for (unsigned i = 0; i < ROUNDS; ++i)
    {
      // Values that must survive a trap, in callee- and caller-saved
      // integer registers and in an FP register.
      // XLEN-wide values; the FP register only on a hart with an FPU.
      const std::uintptr_t s2_want
          = static_cast<std::uintptr_t> (0x5252525200000000ULL) + 0x5200 + i;
      const std::uintptr_t t3_want
          = static_cast<std::uintptr_t> (0x7373737300000000ULL) + 0x7300 + i;
      register std::uintptr_t s2 __asm__ ("s2") = s2_want;
      register std::uintptr_t t3 __asm__ ("t3") = t3_want;
#if defined(__riscv_flen)
      register double fs3 __asm__ ("fs3") = 1.5 * (i + 1);
#define EXC_FP_OPERAND , "+f"(fs3)
#else
#define EXC_FP_OPERAND
#endif

      __asm__ volatile(".option push\n"
                       ".option norvc\n"
                       ".word 0xc0001073\n" // unimp
                       ".option pop\n"
                       : "+r"(s2), "+r"(t3) EXC_FP_OPERAND
                       :
                       : "memory");
      __asm__ volatile("ecall" : "+r"(s2), "+r"(t3) EXC_FP_OPERAND : : "memory");

#if defined(QEMU_BUILD)
      // Nothing is mapped at 0x100 on QEMU virt (the boot ROM starts at
      // 0x1000). On the CV1800B the low addresses are not documented as a
      // hole, so the hardware image does not probe them.
      __asm__ volatile("lw zero, 0x100(zero)"
                       : "+r"(s2), "+r"(t3) EXC_FP_OPERAND
                       :
                       : "memory");
#endif

      if (s2 != s2_want || t3 != t3_want
#if defined(__riscv_flen)
          || fs3 != 1.5 * (i + 1)
#endif
      )
        {
          regs_ok = false;
        }
      if ((i % 10) == 0)
        {
          sysclock.sleep_for (2); // let the ticker and the tick interleave
        }
    }

  stop = true;
  th.join ();

#if defined(QEMU_BUILD)
  const unsigned want_access = ROUNDS;
#else
  const unsigned want_access = 0;
#endif

  char b[160];
  std::snprintf (b, sizeof (b),
                 "illegal=%u ecall=%u access=%u other=%u ticker=%u regs=%s\n",
                 n_illegal, n_ecall, n_access, n_other, ticker_runs,
                 regs_ok ? "ok" : "CORRUPT");
  uart::write (b);

  const bool ok = n_illegal == ROUNDS && n_ecall == ROUNDS
                  && n_access == want_access && n_other == 0 && regs_ok
                  && ticker_runs > 5;
  if (ok)
    {
      uart::write ("RESULT: PASS (exc-test)\n");
      hw_result::ok ();
    }
  uart::write ("RESULT: FAIL (exc-test)\n");
  hw_result::fail ();
  for (;;)
    {
      sysclock.sleep_for (1000);
    }
}
