/*
 * flash_rp2350.h -- RAM-resident QSPI flash writer for the RP2350 on its
 * Hazard3 cores: the predecessor's Cortex-M driver API (micro-os-plus-iii-
 * pico2, src/xip-flash/flash_rp2350.h), on the pico-sdk's RP2350 sequence
 * (hardware_flash/flash.c) through the boot ROM's RISC-V entry points.
 *
 * Offsets are flash offsets from 0x10000000, not addresses. Erase: offset and
 * length 4 KiB-aligned. Program: offset and length 256 B-aligned, data in RAM.
 *
 * While an erase/program runs, XIP is off: nothing may be fetched or read
 * from flash or PSRAM. The routines run from SRAM with this hart's MIE off;
 * the OTHER hart must not touch flash either -- park it with
 * mu_flash_core1_lockout() (or run before it is launched). The stack of the
 * caller must be in SRAM (not PSRAM).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef MU_FLASH_RP2350_H_
#define MU_FLASH_RP2350_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MU_FLASH_XIP_BASE 0x10000000u
#define MU_FLASH_SECTOR 4096u /* erase granularity */
#define MU_FLASH_PAGE 256u    /* program granularity */

  /* Erase [offset, offset + bytes), both 4 KiB-aligned. */
  void mu_flash_erase (uint32_t offset, size_t bytes);

  /* Program `bytes` (a multiple of 256) from `data` (in RAM) at `offset`
   * (256-aligned). */
  void mu_flash_program (uint32_t offset, const void* data, size_t bytes);

  /* Drop the XIP cache's lines so reads see freshly written flash. */
  void mu_flash_xip_flush (void);

  /* On the OTHER hart, while this one writes the flash: MIE off, *ack = 1,
   * spin in SRAM until *release != 0, *ack = 0, MIE back. Both flags in SRAM.
   * Protocol (writer):  release = 0; req = 1; wait ack; erase/program;
   *                     req = 0; release = 1; wait !ack. */
  void mu_flash_core1_lockout (volatile uint32_t* ack,
                               volatile uint32_t* release);

  /* What the last operation used to re-enter XIP: 1 = the boot ROM's XIP
   * setup stub (copied from boot RAM), 0 = flash_enter_cmd_xip (03h serial
   * reads; the stub was empty). For the test logs. */
  extern volatile uint32_t mu_flash_xip_via_stub;

#ifdef __cplusplus
}
#endif
#endif /* MU_FLASH_RP2350_H_ */
