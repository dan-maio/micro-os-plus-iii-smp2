/*
 * key.hpp -- the CH32V307V-EVT user key S2 on PA1, active low (pressed
 * pulls PA1 to ground; the input's pull-up holds it high otherwise).
 *
 * S2 is not connected to the chip on the EVT: it reaches an I/O only
 * through the J3 header (WCH Evaluation Board Reference V1.9, "USER button
 * S2: Connect the I/O port of the main MCU through J3 pin"), so it needs a
 * jumper wire from J3 to PA1, as LED1 needs one to PD0. PA1 is Zephyr's
 * choice for this board.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#pragma once

namespace key
{
  void
  init (void);

  bool
  pressed (void);
} // namespace key
