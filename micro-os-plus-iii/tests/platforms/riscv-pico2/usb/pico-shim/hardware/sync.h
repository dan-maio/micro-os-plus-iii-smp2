#ifndef _MU_PICO_SHIM_SYNC_H
#define _MU_PICO_SHIM_SYNC_H
#include <stdint.h>
__attribute__((always_inline)) static inline void __dmb(void){ __asm volatile("fence rw, rw":::"memory"); }
__attribute__((always_inline)) static inline void __dsb(void){ __asm volatile("fence rw, rw":::"memory"); }
__attribute__((always_inline)) static inline void __isb(void){ __asm volatile("fence.i":::"memory"); }
__attribute__((always_inline)) static inline void __wfe(void){ __asm volatile("nop"); /* RISC-V port: no wfe/wfi */ }
__attribute__((always_inline)) static inline void __sev(void){ __asm volatile("slt x0, x0, x1"); /* Hazard3 h3.unblock */ }
__attribute__((always_inline)) static inline void __wfi(void){ __asm volatile("nop"); }
__attribute__((always_inline)) static inline void __mem_fence_acquire(void){ __dmb(); }
__attribute__((always_inline)) static inline void __mem_fence_release(void){ __dmb(); }
#define remove_volatile_cast(t, x) (t)(x)
#define remove_volatile_cast_no_barrier(t, x) (t)(x)
#endif
