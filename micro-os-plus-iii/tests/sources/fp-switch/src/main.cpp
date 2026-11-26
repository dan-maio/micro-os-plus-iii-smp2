/*
 * This file is part of the µOS++ project (https://micro-os-plus.github.io/).
 * Copyright (c) 2026 Liviu Ionescu. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose is hereby granted, under the terms of the MIT license.
 *
 * If a copy of the license was not distributed with this file, it can be
 * obtained from https://opensource.org/licenses/mit.
 */

// fp-switch: the FPU registers survive a preemptive context switch.
//
// Several unpinned threads, at three priorities, each load a pattern of their
// own into all of s0-s31 and the FPSCR flags, spin long enough for the tick to
// preempt them (and, on SMP, for another core to pick them up), then read the
// registers back. A port that does not save the FPU context on a switch hands
// a thread another thread's registers, or another core's: a mismatch.
//
// The registers are loaded and checked in inline assembly, so the result does
// not depend on how the compiler happens to allocate them.
//
// RISC-V with an FPU (__riscv_flen): the same test on f0-f31 (FLEN bits each:
// fld/fsd with the D extension) and the fflags exception flags of fcsr.

#include <cmsis-plus/rtos/os.h>

#include <cstdio>
#include <cstdint>

using namespace os;
using namespace os::rtos;

#if defined(__ARM_FP) || defined(__riscv_flen)

#if defined(OS_USE_SMP_SCHEDULER)
extern "C" unsigned port_cpu_id (void);
#endif

namespace
{
  constexpr unsigned threads_count = 6;
  constexpr unsigned run_ticks = 3000;
  constexpr unsigned spin_loops = 40000;
#if defined(__riscv_flen)
  // RISC-V: the spin is calibrated at start-up (os_main) so that one hold
  // lasts about hold_ticks ticks whatever the core's speed. The fixed 40000
  // loops are a tick on the MCUs this test was written for, but a 1.5 GHz
  // U74 ran them in about 0.03 ms: a hold was then almost never preempted,
  // and no thread ever resumed a hold on another core (0 migrated).
  constexpr unsigned hold_ticks = 3;
  unsigned long spin_n = spin_loops;
#endif
#if defined(__ARM_FP)
  // One FPU register (s0-s31).
  using fp_reg_t = uint32_t;
  // FPSCR N, Z, C, V: flags only, the rounding and exception bits are left.
  constexpr uint32_t fp_flags_mask = 0xF0000000u;
  constexpr unsigned fp_flags_shift = 28;
#else
  // One FPU register (f0-f31), FLEN bits.
#if __riscv_flen == 64
  using fp_reg_t = uint64_t;
#else
  using fp_reg_t = uint32_t;
#endif
  // fcsr.fflags NV, DZ, OF, UF, NX: flags only, the rounding mode is left.
  constexpr uint32_t fp_flags_mask = 0x1Fu;
  constexpr unsigned fp_flags_shift = 0;
#endif

  volatile bool stop;

  struct context
  {
    unsigned id;
    unsigned rounds;
    unsigned bad_rounds;
    unsigned migrated_rounds;
  };

  context contexts[threads_count];

  // Load in[] into s0-s31 and flags into the FPSCR, spin, store the
  // registers to out[] and return the FPSCR flags read back. The caller's
  // FPSCR is restored.
#if defined(__ARM_FP)
  uint32_t
  hold_fp_registers (const fp_reg_t* in, fp_reg_t* out, uint32_t flags)
  {
    uint32_t saved;
    uint32_t read;
    uint32_t n = spin_loops;
    __asm__ volatile (
        " vmrs %[saved], fpscr               \n"
        " vldmia %[in]!, {s0-s15}             \n"
        " vldmia %[in]!, {s16-s31}            \n"
        " bic %[read], %[saved], %[mask]      \n"
        " orr %[read], %[read], %[flags]      \n"
        " vmsr fpscr, %[read]                 \n"
        "1:                                   \n"
        " subs %[n], %[n], #1                 \n"
        " bne 1b                              \n"
        " vmrs %[read], fpscr                 \n"
        " vstmia %[out]!, {s0-s15}            \n"
        " vstmia %[out]!, {s16-s31}           \n"
        " vmsr fpscr, %[saved]                \n"
        : [saved] "=&r"(saved), [read] "=&r"(read), [n] "+r"(n),
          [in] "+r"(in), [out] "+r"(out)
        : [flags] "r"(flags), [mask] "r"(fp_flags_mask)
        : "memory", "cc", "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
          "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15", "s16", "s17",
          "s18", "s19", "s20", "s21", "s22", "s23", "s24", "s25", "s26",
          "s27", "s28", "s29", "s30", "s31");
    return read & fp_flags_mask;
  }
#else
#if __riscv_flen == 64
#define FP_SWITCH_L "fld"
#define FP_SWITCH_S "fsd"
#else
#define FP_SWITCH_L "flw"
#define FP_SWITCH_S "fsw"
#endif
#define FP_SWITCH_ONE(op, r, i, p) " " op " " #r ", " #i "*%c[sz](%[" p "]) \n"
#define FP_SWITCH_ALL(op, p)                                                  \
  FP_SWITCH_ONE (op, f0, 0, p) FP_SWITCH_ONE (op, f1, 1, p)                   \
  FP_SWITCH_ONE (op, f2, 2, p) FP_SWITCH_ONE (op, f3, 3, p)                   \
  FP_SWITCH_ONE (op, f4, 4, p) FP_SWITCH_ONE (op, f5, 5, p)                   \
  FP_SWITCH_ONE (op, f6, 6, p) FP_SWITCH_ONE (op, f7, 7, p)                   \
  FP_SWITCH_ONE (op, f8, 8, p) FP_SWITCH_ONE (op, f9, 9, p)                   \
  FP_SWITCH_ONE (op, f10, 10, p) FP_SWITCH_ONE (op, f11, 11, p)               \
  FP_SWITCH_ONE (op, f12, 12, p) FP_SWITCH_ONE (op, f13, 13, p)               \
  FP_SWITCH_ONE (op, f14, 14, p) FP_SWITCH_ONE (op, f15, 15, p)               \
  FP_SWITCH_ONE (op, f16, 16, p) FP_SWITCH_ONE (op, f17, 17, p)               \
  FP_SWITCH_ONE (op, f18, 18, p) FP_SWITCH_ONE (op, f19, 19, p)               \
  FP_SWITCH_ONE (op, f20, 20, p) FP_SWITCH_ONE (op, f21, 21, p)               \
  FP_SWITCH_ONE (op, f22, 22, p) FP_SWITCH_ONE (op, f23, 23, p)               \
  FP_SWITCH_ONE (op, f24, 24, p) FP_SWITCH_ONE (op, f25, 25, p)               \
  FP_SWITCH_ONE (op, f26, 26, p) FP_SWITCH_ONE (op, f27, 27, p)               \
  FP_SWITCH_ONE (op, f28, 28, p) FP_SWITCH_ONE (op, f29, 29, p)               \
  FP_SWITCH_ONE (op, f30, 30, p) FP_SWITCH_ONE (op, f31, 31, p)

  // Load in[] into f0-f31 and flags into fcsr.fflags, spin, store the
  // registers to out[] and return the fflags read back. The caller's fcsr
  // is restored. One asm statement, so the compiler cannot touch an FP
  // register in between.
  uint32_t
  hold_fp_registers (const fp_reg_t* in, fp_reg_t* out, uint32_t flags)
  {
    unsigned long saved;
    unsigned long read;
    unsigned long n = spin_n;
    __asm__ volatile (
        " frcsr %[saved]                      \n"
        FP_SWITCH_ALL (FP_SWITCH_L, "in")
        " fsflags %[flags]                    \n"
        "1:                                   \n"
        " addi %[n], %[n], -1                 \n"
        " bnez %[n], 1b                       \n"
        " frflags %[read]                     \n"
        FP_SWITCH_ALL (FP_SWITCH_S, "out")
        " fscsr %[saved]                      \n"
        : [saved] "=&r"(saved), [read] "=&r"(read), [n] "+r"(n)
        : [in] "r"(in), [out] "r"(out),
          [flags] "r"(static_cast<unsigned long> (flags)),
          [sz] "i"(sizeof (fp_reg_t))
        : "memory", "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8",
          "f9", "f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f18",
          "f19", "f20", "f21", "f22", "f23", "f24", "f25", "f26", "f27",
          "f28", "f29", "f30", "f31");
    return static_cast<uint32_t> (read) & fp_flags_mask;
  }
#endif

  void*
  worker (void* arg)
  {
    context* ctx = static_cast<context*> (arg);
    fp_reg_t in[32];
    fp_reg_t out[32];

    while (!stop)
      {
        for (unsigned k = 0; k < 32; ++k)
          {
            const uint32_t v = 0x40000000u | (ctx->id << 16)
                               | ((ctx->rounds & 0xFFu) << 8) | k;
            in[k] = static_cast<fp_reg_t> (v);
            if constexpr (sizeof (fp_reg_t) == 8)
              {
                // Both halves: a 64-bit register must come back whole.
                in[k] = (static_cast<fp_reg_t> (v) << 32) | (v ^ 0xA5A5A5A5u);
              }
          }
        uint32_t flags = ((ctx->id + ctx->rounds) << fp_flags_shift)
                         & fp_flags_mask;

#if defined(OS_USE_SMP_SCHEDULER)
        unsigned cpu_before = port_cpu_id ();
#endif
        uint32_t flags_read = hold_fp_registers (in, out, flags);
#if defined(OS_USE_SMP_SCHEDULER)
        if (port_cpu_id () != cpu_before)
          {
            ++ctx->migrated_rounds;
          }
#endif

        bool bad = (flags_read != flags);
        for (unsigned k = 0; k < 32; ++k)
          {
            if (out[k] != in[k])
              {
                bad = true;
              }
          }
        if (bad)
          {
            ++ctx->bad_rounds;
          }
        ++ctx->rounds;

        // Sleep now and then, so the higher priority threads wake up in the
        // middle of the lower ones' rounds.
        if (ctx->rounds % (ctx->id + 2) == 0)
          {
            sysclock.sleep_for (1);
          }
      }
    return nullptr;
  }
} // namespace

int
os_main (int argc __attribute__ ((unused)),
         char* argv[] __attribute__ ((unused)))
{
  // The workers spin for most of each tick and at up to normal + 2 they can
  // take the whole CPU. On real silicon a round is about a tick long, so at
  // the default (normal) priority this thread never ran again: it never set
  // `stop` and the test never ended. Keep the controller above the workers.
  this_thread::thread ().priority (thread::priority::high);

#if defined(__riscv_flen)
  {
    // Calibrate: time holds of a growing spin until one takes at least
    // 8 ticks, then scale to hold_ticks.
    fp_reg_t in[32] = {};
    fp_reg_t out[32];
    unsigned long n = spin_loops;
    for (;;)
      {
        spin_n = n;
        sysclock.sleep_for (1); // start on a tick boundary
        const auto t0 = sysclock.now ();
        hold_fp_registers (in, out, 0);
        const auto dt = sysclock.now () - t0;
        // The cap keeps n * 4 (and n * hold_ticks) inside unsigned long:
        // 1ul << 40 was undefined on RV32, where unsigned long is 32 bits.
        if (dt >= 8 || n > (~0ul / 16u))
          {
            spin_n = (n * hold_ticks) / (dt != 0 ? dt : 1);
            if (spin_n == 0)
              {
                spin_n = 1;
              }
            break;
          }
        n *= 4;
      }
    std::printf ("\nhold: %lu spin loops, about %u ticks\n", spin_n,
                 hold_ticks);
  }
#endif

  std::printf ("\nFPU context switch test, %u threads, %u ticks\n",
               threads_count, run_ticks);

  thread* threads[threads_count];
  for (unsigned i = 0; i < threads_count; ++i)
    {
      contexts[i].id = i;
      thread::attributes attr;
      attr.th_priority
          = static_cast<thread::priority_t> (thread::priority::normal + i % 3);
      attr.th_stack_size_bytes = 2048;
      threads[i] = new thread ("fp", worker, &contexts[i], attr);
    }

  sysclock.sleep_for (run_ticks);
  stop = true;

  unsigned rounds = 0;
  unsigned bad = 0;
  unsigned migrated = 0;
  for (unsigned i = 0; i < threads_count; ++i)
    {
      threads[i]->join ();
      delete threads[i];
      std::printf ("thread %u: %u rounds, %u bad, %u migrated\n", i,
                   contexts[i].rounds, contexts[i].bad_rounds,
                   contexts[i].migrated_rounds);
      rounds += contexts[i].rounds;
      bad += contexts[i].bad_rounds;
      migrated += contexts[i].migrated_rounds;
    }

  std::printf ("total: %u rounds, %u bad, %u migrated\n", rounds, bad,
               migrated);
#if defined(__riscv_flen) && defined(OS_USE_SMP_SCHEDULER)
  // With several cores, a hold must also have survived a move to another
  // core: without one, the cross-core FPU save/restore was not tested.
  if (migrated == 0)
    {
      std::printf ("no round migrated to another core\n");
      return 1;
    }
#endif
  return (bad == 0 && rounds > 0) ? 0 : 1;
}

#else

int
os_main (int argc __attribute__ ((unused)),
         char* argv[] __attribute__ ((unused)))
{
  std::printf ("\nFPU context switch test: no FPU, nothing to test\n");
  return 0;
}

#endif // defined(__ARM_FP) || defined(__riscv_flen)
