/*
 * dwc2_cv180x.h -- TinyUSB DWC2 platform header for the Sophgo CV180x (the
 * Milk-V Duo's CV1800B), this tree's addition to the upstream driver
 * (TinyUSB commit 91fbbd192, src/portable/synopsys/dwc2), on the model of
 * dwc2_bcm.h.
 *
 * Measured on the board (2026-11-06): GSNPSID 0x4F54420A (OTG 4.20a),
 * GHWCFG2 0x228F5C52 (internal DMA capable, UTMI+ HS PHY, 8 device
 * endpoints), GHWCFG3 0x0C0004E8 (3072-word FIFO), GHWCFG4 0xDA00BA30
 * (dedicated TX FIFOs, 7 IN endpoints). The C906 sees the controller as PLIC
 * source 30 (SG2002 TRM table 3.2).
 *
 * Used in slave (PIO) mode: no DMA, so no cache maintenance for the C906's
 * D-cache. The clocks, resets and PHY are set up before tusb_init() by
 * duo_usb_hw_init() (duo-usb.c, BL2's sequence): the PHY hooks have nothing
 * left to do.
 *
 * Copyright (c) 2026 Dan. MIT license.
 */
#ifndef TUSB_DWC2_CV180X_H_
#define TUSB_DWC2_CV180X_H_

#ifdef __cplusplus
extern "C" {
#endif

#define DWC2_EP_MAX 8

static const dwc2_controller_t _dwc2_controller[] = {
  { .reg_base = 0x04340000u, .irqnum = 30, .ep_count = 8, .ep_in_count = 7,
    .otg_dfifo_depth = 3072 }
};

// duo-usb.c: the PLIC through the board's duo_irq.
void duo_usb_irq_enable (void);
void duo_usb_irq_disable (void);
void duo_usb_udelay (unsigned int us);

TU_ATTR_ALWAYS_INLINE static inline void
dwc2_clock_init (uint8_t rhport, tusb_role_t role)
{
  (void) rhport;
  (void) role;
}

TU_ATTR_ALWAYS_INLINE static inline void
dwc2_dcd_int_enable (uint8_t rhport)
{
  (void) rhport;
  duo_usb_irq_enable ();
}

TU_ATTR_ALWAYS_INLINE static inline void
dwc2_dcd_int_disable (uint8_t rhport)
{
  (void) rhport;
  duo_usb_irq_disable ();
}

static inline void
dwc2_remote_wakeup_delay (void)
{
}

static inline void
dwc2_phy_init (dwc2_regs_t* dwc2, uint8_t hs_phy_type)
{
  (void) dwc2;
  (void) hs_phy_type;
}

static inline void
dwc2_phy_deinit (dwc2_regs_t* dwc2, uint8_t hs_phy_type)
{
  (void) dwc2;
  (void) hs_phy_type;
}

static inline void
dwc2_phy_update (dwc2_regs_t* dwc2, uint8_t hs_phy_type)
{
  (void) dwc2;
  (void) hs_phy_type;
}

// Slave mode: the driver moves the data through the FIFOs itself.
TU_ATTR_ALWAYS_INLINE static inline bool
dwc2_dcache_clean (const void* addr, uint32_t data_size)
{
  (void) addr;
  (void) data_size;
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool
dwc2_dcache_invalidate (const void* addr, uint32_t data_size)
{
  (void) addr;
  (void) data_size;
  return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool
dwc2_dcache_clean_invalidate (const void* addr, uint32_t data_size)
{
  (void) addr;
  (void) data_size;
  return true;
}

#ifdef __cplusplus
}
#endif

#endif
