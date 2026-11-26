/* arch/cc.h -- compiler/platform glue for lwIP on the VisionFive 2 Lite.
 *
 * RV64, GCC, newlib: only the byte order, the libc ssize_t clash, and the
 * diagnostics/RNG hooks lwIP asks the port for.  kprintf() ends up on UART0
 * through the board's console (see board/uart.cpp).
 */
#ifndef VF2_NET_ARCH_CC_H
#define VF2_NET_ARCH_CC_H

#define BYTE_ORDER LITTLE_ENDIAN

/* lwIP typedefs ssize_t as 'int' unless SSIZE_MAX is defined, which clashes
 * with the toolchain's <stdio.h> ('long'). */
#define SSIZE_MAX 0x7fffffffffffffffL

#ifdef __cplusplus
extern "C"
{
#endif
  void kprintf (const char* format, ...)
      __attribute__ ((format (printf, 1, 2)));
  unsigned int net_random (void);
#ifdef __cplusplus
}
#endif

#define LWIP_PLATFORM_DIAG(x)                                                  \
  do                                                                           \
    {                                                                          \
      kprintf x;                                                               \
    }                                                                          \
  while (0)
#define LWIP_PLATFORM_ASSERT(x)                                                \
  do                                                                           \
    {                                                                          \
      kprintf ("[net] lwIP assertion \"%s\" at %s:%d\n", x, __FILE__,          \
               __LINE__);                                                      \
    }                                                                          \
  while (0)
#define LWIP_RAND() net_random ()

#endif
