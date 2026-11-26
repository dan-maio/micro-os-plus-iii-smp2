/*
 * led.hpp -- QEMU virt has no LED: a variable stands in for it, so a test's
 * LED logic still runs and can be checked (is_on()).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

namespace led
{
  void
  init (void);

  void
  on (void);

  void
  off (void);

  void
  toggle (void);

  bool
  is_on (void);
} // namespace led
