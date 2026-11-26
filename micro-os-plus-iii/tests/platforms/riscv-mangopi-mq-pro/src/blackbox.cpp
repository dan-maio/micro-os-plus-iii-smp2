/*
 * blackbox.cpp -- the RAM console log described in blackbox.hpp.
 *
 * Deliberately free of kernel and libc dependencies: the board's sources are
 * linked into every test, including the bare ones that have no scheduler.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include <blackbox.hpp>

#include <board.hpp>
#include <cstddef>
#include <cstdint>

namespace
{
  struct header
  {
    std::uint32_t magic;
    std::uint32_t length;
    std::uint32_t lost;
    std::uint32_t reserved;
  };

  inline volatile header&
  hdr (void)
  {
    return *reinterpret_cast<volatile header*> (board::SCRATCH_BASE);
  }

  inline volatile char*
  text (void)
  {
    return reinterpret_cast<volatile char*> (board::SCRATCH_BASE
                                             + sizeof (header));
  }
} // namespace

namespace blackbox
{
  void
  reset (void)
  {
    hdr ().magic = 0;
    hdr ().length = 0;
    hdr ().lost = 0;
    hdr ().reserved = 0;
    hdr ().magic = magic;
  }

  void
  put (char c)
  {
    const std::uint32_t n = hdr ().length;
    if (n + 1u < capacity)
      {
        text ()[n] = c;
        // NUL-terminate as we go: the reader can then stop at the end of the
        // text even if the header word is still in the D-cache when the SoC
        // resets (the text itself is evicted to DDR, the hot header is not).
        text ()[n + 1u] = '\0';
        __asm__ volatile("" ::: "memory");
        hdr ().length = n + 1u;
      }
    else
      {
        hdr ().lost = hdr ().lost + 1u;
      }
  }

  std::uint32_t
  length (void)
  {
    return hdr ().length;
  }
} // namespace blackbox
