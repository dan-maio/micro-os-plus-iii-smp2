/*
 * psram_app.inc -- a self-contained application that lives ENTIRELY in the
 * PSRAM: its own header, code, rodata, data, bss and trap handler. Included
 * twice (psram_app_a.cpp, psram_app_b.cpp): APP_SEC_STR picks the sections
 * (.psram_a_* at 0x11000000, .psram_b_* at 0x11400000, linker-psram-exec.ld).
 *
 * RISC-V port of the Cortex-M pico2-pizero board's psram_app.inc. Arm's
 * vector table becomes a header {magic, entry, trap}: the supervisor points
 * mtvec at `trap` while the application runs and calls `entry` through a
 * register. On its first activation the application takes an ecall, so its
 * own trap handler -- in the PSRAM -- runs and returns (counted).
 *
 * Everything here is file-local and forced inline where it is a helper, so no
 * call leaves the PSRAM (checked in the disassembly).
 */

#include <cstdint>

#include <rp2350.hpp>

#ifndef APP_SEC_STR
#define APP_SEC_STR "a"
#endif
#ifndef APP_IS_A
#define APP_IS_A 1
#endif

#define SEC(suffix)                                                           \
  __attribute__ ((section (".psram_" APP_SEC_STR suffix), used))
#define INL __attribute__ ((always_inline)) inline

#if APP_IS_A
#define APP_LETTER "A"
#define APP_MAGIC 0x5A5A
#else
#define APP_LETTER "B"
#define APP_MAGIC 0xB00B
#endif

// The supervisor's (SRAM): an application writes 2 here when the host
// pressed 'x'.
extern volatile unsigned g_psram_stop;

namespace
{
  INL volatile std::uint32_t&
  app_reg (std::uint32_t a)
  {
    return *reinterpret_cast<volatile std::uint32_t*> (a);
  }
  constexpr std::uint32_t UART0 = rp2350::UART0_BASE;
  constexpr std::uint32_t UART_DR = 0x00u, UART_FR = 0x18u;
  constexpr std::uint32_t FR_TXFF = 1u << 5, FR_RXFE = 1u << 4;
  constexpr std::uint32_t SIO = rp2350::SIO_BASE;
  constexpr std::uint32_t SIO_OUT_SET = 0x018u, SIO_OUT_CLR = 0x020u,
                          SIO_OE_SET = 0x038u;
  constexpr std::uint32_t APP_LED_PIN = rp2350::LED_PIN, GPIO_FUNC_SIO = 5u;
  constexpr std::uint32_t PADS_ISO = 1u << 8, PADS_IE = 1u << 6;
} // namespace

SEC ("_rodata") static const char MSG_BANNER[]
    = "\r\n----------------------------------------\r\n"
      "  APP " APP_LETTER " is now ACTIVE (from PSRAM)\r\n"
      "  press ESC to switch apps, x to finish\r\n"
      "----------------------------------------\r\n";
SEC ("_rodata") static const char MSG_HB[] = "  APP " APP_LETTER " heartbeat #";
SEC ("_rodata") static const char MSG_TRAP[] = "  APP " APP_LETTER " own trap handler (PSRAM) ran: ecall count ";
SEC ("_rodata") static const char MSG_NL[] = "\r\n";
SEC ("_rodata") static const char MSG_BADMAGIC[] = "  .data magic WRONG\r\n";

SEC ("_data") static volatile int app_magic = APP_MAGIC; // initialized
SEC ("_bss") static volatile int app_counter;            // zero
SEC ("_bss") static volatile int app_ecalls;             // zero

static SEC ("_text") void
app_putc (char c)
{
  while (app_reg (UART0 + UART_FR) & FR_TXFF)
    {
    }
  app_reg (UART0 + UART_DR)
      = static_cast<std::uint32_t> (static_cast<std::uint8_t> (c));
}

static SEC ("_text") void
app_print (const char* s)
{
  while (*s != '\0')
    {
      app_putc (*s++);
    }
}

static SEC ("_text") void
app_print_dec (int v)
{
  char b[12];
  int n = 0;
  unsigned u = static_cast<unsigned> (v);
  do
    {
      b[n++] = static_cast<char> ('0' + (u % 10));
      u /= 10;
    }
  while (u != 0);
  while (n > 0)
    {
      app_putc (b[--n]);
    }
}

static SEC ("_text") void
app_led_init (void)
{
  volatile std::uint32_t& pad
      = app_reg (rp2350::PADS_BANK0_BASE + 0x04u + APP_LED_PIN * 4u);
  pad = (pad & ~PADS_ISO) | PADS_IE;
  volatile std::uint32_t& ctrl
      = app_reg (rp2350::IO_BANK0_BASE + APP_LED_PIN * 8u + 4u);
  ctrl = (ctrl & ~0x1Fu) | GPIO_FUNC_SIO;
  app_reg (SIO + SIO_OE_SET) = (1u << APP_LED_PIN);
}

static SEC ("_text") void
app_led (bool on)
{
  app_reg (SIO + (on ? SIO_OUT_SET : SIO_OUT_CLR)) = (1u << APP_LED_PIN);
}

// The application's own trap handler, in the PSRAM (mtvec while it runs).
// An ecall from M-mode (mcause 11): count it and step over it. Anything else
// is not expected: stop here.
static SEC ("_text") __attribute__ ((interrupt ("machine"), aligned (4))) void
app_trap (void)
{
  std::uint32_t cause, epc;
  __asm__ volatile("csrr %0, mcause" : "=r"(cause));
  if (cause == 11u)
    {
      app_ecalls = app_ecalls + 1;
      __asm__ volatile("csrr %0, mepc" : "=r"(epc));
      __asm__ volatile("csrw mepc, %0" ::"r"(epc + 4u));
      return;
    }
  for (;;)
    {
    }
}

// The entry: announce, take one ecall into the own trap handler, then a
// heartbeat until ESC (back to the supervisor, which switches) or 'x' (the
// supervisor ends the run). State survives in this application's .bss.
static SEC ("_text") void
app_entry (void)
{
  app_print (MSG_BANNER);
  if (app_magic != APP_MAGIC)
    {
      app_print (MSG_BADMAGIC);
    }
  {
    register std::uint32_t a7 __asm__ ("a7") = 0; // not the port's yield code
    __asm__ volatile("ecall" : "+r"(a7)::"memory");
  }
  app_print (MSG_TRAP);
  app_print_dec (app_ecalls);
  app_print (MSG_NL);
  app_led_init ();
  for (;;)
    {
      app_counter = app_counter + 1;
      app_led ((app_counter & 1) != 0);
      app_print (MSG_HB);
      app_print_dec (app_counter);
      app_print (MSG_NL);

      // Poll the UART; only ESC (0x1B) or 'x' act, every other byte is read
      // and dropped (echo, noise), so the RX FIFO stays drained.
      for (volatile std::uint32_t d = 0; d < 3000000u; ++d)
        {
          if ((app_reg (UART0 + UART_FR) & FR_RXFE) == 0)
            {
              const std::uint32_t dr = app_reg (UART0 + UART_DR);
              if ((dr & 0xF00u) == 0)
                {
                  const std::uint32_t c = dr & 0xFFu;
                  if (c == 0x1Bu)
                    {
                      app_led (false);
                      return;
                    }
                  if (c == 0x78u)
                    {
                      app_led (false);
                      g_psram_stop = 2u;
                      return;
                    }
                }
            }
        }
    }
}

// The header, first in the image: what the supervisor reads.
struct app_header_t
{
  std::uint32_t magic;
  void (*entry) (void);
  void (*trap) (void);
};

SEC ("_vectors") const app_header_t app_header = {
  0x50534558u, // 'PSEX'
  app_entry,
  app_trap,
};
