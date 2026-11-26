/*
 * dcd_cdns3.c -- TinyUSB device controller driver for the Cadence USBSS-DRD
 * device controller (cdns3, "USBSS-DEV"), as on the StarFive JH-7110
 * (VisionFive 2 Lite; device controller version 0x0002450d, measured).
 *
 * TinyUSB has no Cadence driver; this one follows Linux's
 * drivers/usb/cdns3/cdns3-gadget.c, cdns3-ep0.c and cdns3-gadget.h
 * (register map, bits, sequences), reduced to what a TinyUSB device needs:
 *
 *   - polled: dcd_int_handler() is called from the USB thread (the PLIC
 *     line is not used); dcd_int_enable/disable do nothing;
 *   - full speed only (USB_CONF_SFORCE_FS): 64-byte EP0 and bulk packets,
 *     so the CFG_TUD descriptors need no high-speed variant;
 *   - endpoint 0 uses a two-TRB pool shared by both directions (Linux:
 *     cdns3_ep0_run_transfer). The controller sends the STATUS stage
 *     itself once software writes EP_CMD_REQ_CMPL (cdns3_ep0_complete_
 *     setup); TinyUSB instead queues a zero-length status transfer, which
 *     this driver turns into REQ_CMPL (+ ERDY when there was no data
 *     stage) and completes at once. SET_ADDRESS: TinyUSB expects the
 *     driver to answer the status itself (usbd.c), done in
 *     dcd_set_address(). SET_CONFIGURATION: USB_CONF_CFGSET before
 *     REQ_CMPL, then wait for USB_STS.CFGSTS (cdns3_gadget_ep0_queue);
 *     transfers queued while the hardware is not configured are started
 *     after CFGSET (cdns3_set_hw_configuration);
 *   - data endpoints: a ring of RING_TRBS TRBs whose last one is a LINK
 *     back to the start with TOGGLE (cdns3_allocate_trb_pool), one TRB per
 *     transfer, producer cycle state taken from EP_STS.CCS after EPRST
 *     (cdns3_gadget_ep_enable), EP_TRADDR written once per enable (DMULT,
 *     cdns3_ep_run_transfer);
 *   - DMA is not cache-coherent on the JH-7110: every TRB and buffer is
 *     written back / invalidated through vf2_dcache_flush() (the L2
 *     controller's FLUSH64, as Linux does for this SoC).
 *
 * Copyright (c) 2026 Dan. MIT license.
 */

#include "tusb_option.h"

#if CFG_TUD_ENABLED && (CFG_TUSB_MCU == OPT_MCU_JH7110)

#include "device/dcd.h"

#include <string.h>

#include <vf2-usb.h>

//--------------------------------------------------------------------+
// Registers (cdns3-gadget.h, struct cdns3_usb_regs)
//--------------------------------------------------------------------+
#define DEV_BASE 0x10120000u

#define USB_CONF 0x000
#define USB_STS 0x004
#define USB_CMD 0x008
#define USB_IEN 0x014
#define USB_ISTS 0x018
#define EP_SEL 0x01c
#define EP_TRADDR 0x020
#define EP_CFG 0x024
#define EP_CMD 0x028
#define EP_STS 0x02c
#define EP_STS_EN 0x034
#define EP_IEN 0x03c
#define EP_ISTS 0x040
#define USB_PWR 0x044
#define USB_CAP2 0x050
#define USB_CAP6 0x060
#define EP_DMA_EXT_ADDR 0x070
#define DTRANS 0x080
#define TDL_FROM_TRB 0x084
#define TDL_BEH 0x088
#define TDL_BEH2 0x090
#define DMA_ADV_TD 0x094
#define DMA_AXI_CTRL 0x300

// USB_CONF
#define USB_CONF_CFGRST (1u << 0)
#define USB_CONF_CFGSET (1u << 1)
#define USB_CONF_USB3DIS (1u << 3)
#define USB_CONF_CLK2OFFDS (1u << 19)
#define USB_CONF_DEVEN (1u << 14)
#define USB_CONF_DEVDS (1u << 15)
#define USB_CONF_L1DS (1u << 17)
#define USB_CONF_SFORCE_FS (1u << 13)
#define USB_CONF_LGO_L0 (1u << 20)
#define USB_CONF_U1DS (1u << 25)
#define USB_CONF_U2DS (1u << 27)
// USB_STS
#define USB_STS_CFGSTS (1u << 0)
#define USB_STS_SPEED(p) (((p) >> 4) & 0x7u)
// USB_CMD
#define USB_CMD_SET_ADDR (1u << 0)
#define USB_CMD_FADDR(a) (((uint32_t) (a) << 1) & 0xFEu)
// USB_IEN / USB_ISTS
#define USB_ISTS_CONI (1u << 0)
#define USB_ISTS_DISI (1u << 1)
#define USB_ISTS_UWRESI (1u << 2)
#define USB_ISTS_UHRESI (1u << 3)
#define USB_ISTS_U3ENTI (1u << 4)
#define USB_ISTS_U3EXTI (1u << 5)
#define USB_ISTS_CON2I (1u << 16)
#define USB_ISTS_DIS2I (1u << 17)
#define USB_ISTS_U2RESI (1u << 18)
#define USB_ISTS_L2ENTI (1u << 20)
#define USB_ISTS_L2EXTI (1u << 21)
#define USB_IEN_INIT                                                          \
  ((1u << 18) | (1u << 17) | (1u << 16) | (1u << 3) | (1u << 2) | (1u << 1)   \
   | (1u << 0) | (1u << 5) | (1u << 20) | (1u << 21) | (1u << 24) | (1u << 4))
// EP_SEL
#define EP_SEL_DIR (1u << 7)
// EP_CFG
#define EP_CFG_ENABLE (1u << 0)
#define EP_CFG_EPTYPE(t) (((uint32_t) (t) << 1) & 0x6u)
#define EP_CFG_TDL_CHK (1u << 4)
#define EP_CFG_MAXPKTSIZE(p) (((uint32_t) (p) << 16) & 0x07FF0000u)
#define EP_CFG_BUFFERING(b) (((uint32_t) (b) << 27) & 0xF8000000u)
// EP_CMD
#define EP_CMD_EPRST (1u << 0)
#define EP_CMD_SSTALL (1u << 1)
#define EP_CMD_CSTALL (1u << 2)
#define EP_CMD_ERDY (1u << 3)
#define EP_CMD_REQ_CMPL (1u << 5)
#define EP_CMD_DRDY (1u << 6)
// EP_STS
#define EP_STS_SETUP (1u << 0)
#define EP_STS_IOC (1u << 2)
#define EP_STS_ISP (1u << 3)
#define EP_STS_DESCMIS (1u << 4)
#define EP_STS_TRBERR (1u << 7)
#define EP_STS_CCS (1u << 11)
#define EP_STS_IOT (1u << 19)
// EP_STS_EN
#define EP_STS_EN_SETUPEN (1u << 0)
#define EP_STS_EN_DESCMISEN (1u << 4)
#define EP_STS_EN_TRBERREN (1u << 7)
// USB_PWR
#define PUSB_PWR_FST_REG_ACCESS (1u << 31)
// DMA_AXI_CTRL: non-secure (DMA_AXI_CTRL_NON_SECURE 0x02) reads and writes
#define DMA_AXI_CTRL_NON_SECURE ((0x2u) | (0x2u << 16))

#define DEV_VER_V3 0x0002450du

// TRB (struct cdns3_trb)
#define TRB_TYPE(p) ((uint32_t) (p) << 10)
#define TRB_NORMAL 1u
#define TRB_LINK 6u
#define TRB_CYCLE (1u << 0)
#define TRB_TOGGLE (1u << 1)
#define TRB_ISP (1u << 2)
#define TRB_CHAIN (1u << 4)
#define TRB_IOC (1u << 5)
#define TRB_TDL_HS_SIZE(p) (((uint32_t) (p) << 16) & 0xFFFF0000u)
#define TRB_LEN(p) ((uint32_t) (p) & 0x1FFFFu)
#define TRB_BURST_LEN(p) (((uint32_t) (p) << 24) & 0xFF000000u)

typedef struct
{
  volatile uint32_t buffer;
  volatile uint32_t length;
  volatile uint32_t control;
} trb_t;

#define FS_MPS 64u
#define RING_TRBS 4u /* 3 transfer TRBs + the LINK */

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+
typedef struct
{
  trb_t ring[RING_TRBS];
  uint8_t pad[64 - (sizeof (trb_t) * RING_TRBS) % 64];
} ring_t;

typedef struct
{
  bool open;
  bool busy;
  bool deferred; // queued before the hardware was configured
  bool update_traddr;
  uint8_t pcs;
  uint8_t enqueue;
  uint8_t started; // TRB index of the running transfer
  uint8_t type;
  uint16_t mps;
  uint8_t* buf;
  uint16_t len;
} ep_state_t;

static ring_t ep_rings[16][2] __attribute__ ((aligned (64)));
static ep_state_t eps[16][2]; // [num][dir]: dir 0 = OUT, 1 = IN

static trb_t ep0_trb[2] __attribute__ ((aligned (64)));
static uint32_t ep_done_count[16][2]; // diagnostics (dcd_cdns3_ep_done)
static uint8_t setup_buf[64] __attribute__ ((aligned (64)));

static struct
{
  uint32_t dev_ver;
  uint32_t onchip_kb;
  uint32_t onchip_used;
  uint32_t out_mem;
  bool hw_configured;
  bool wait_for_setup;
  bool setup_armed;
  uint8_t ep0_data_dir; // 0 OUT, 0x80 IN (of the current request)
  bool ep0_data_done;   // data stage finished, status next
  tusb_control_request_t setup;
  uint8_t* ep0_buf;
  uint16_t ep0_len;
  uint8_t selected;
} dev;

static inline volatile uint32_t*
reg (uint32_t off)
{
  return (volatile uint32_t*) (uintptr_t) (DEV_BASE + off);
}

static inline uint32_t
rd (uint32_t off)
{
  return *reg (off);
}

static inline void
wr (uint32_t off, uint32_t v)
{
  *reg (off) = v;
}

static inline void
set_bits (uint32_t off, uint32_t bits)
{
  wr (off, rd (off) | bits);
}

static inline uint32_t
phys (const void* p)
{
  return (uint32_t) (uintptr_t) p; // M-mode, no MMU: physical = virtual
}

static void
select_ep (uint8_t ep_addr)
{
  dev.selected = ep_addr;
  wr (EP_SEL, ep_addr);
}

static void
wakeup (void)
{
  wr (USB_CONF, USB_CONF_LGO_L0); // __cdns3_gadget_wakeup (not SuperSpeed)
}

//--------------------------------------------------------------------+
// Endpoint 0 (cdns3-ep0.c)
//--------------------------------------------------------------------+
static void
ep0_run_transfer (uint8_t dir, void* buf, uint32_t length, bool erdy)
{
  if (dir == 0)
    {
      vf2_dcache_flush (buf, length); // no dirty line over the DMA data
    }
  else
    {
      vf2_dcache_flush (buf, length); // the data the DMA will read
    }
  ep0_trb[0].buffer = phys (buf);
  ep0_trb[0].length = TRB_LEN (length);
  ep0_trb[0].control = TRB_CYCLE | TRB_IOC | TRB_TYPE (TRB_NORMAL);
  ep0_trb[1].control = 0;
  vf2_dcache_flush (ep0_trb, sizeof (ep0_trb));

  select_ep (dir);
  wr (EP_STS, EP_STS_TRBERR);
  wr (EP_TRADDR, phys (ep0_trb));
  wr (EP_CMD, EP_CMD_DRDY);
  wakeup ();
  if (erdy)
    {
      wr (EP_CMD, EP_CMD_ERDY);
    }
}

static void
prepare_setup_packet (void)
{
  dev.ep0_data_dir = 0;
  dev.setup_armed = true;
  ep0_run_transfer (0, setup_buf, 8, false);
}

static void
ep0_complete_setup (bool send_stall, bool send_erdy)
{
  if (send_stall)
    {
      select_ep (0x00);
      wr (EP_CMD, EP_CMD_SSTALL);
    }
  else
    {
      prepare_setup_packet ();
    }
  wr (EP_CMD, (send_erdy ? EP_CMD_ERDY : 0) | EP_CMD_REQ_CMPL);
}

static void
ep0_config (void)
{
  const uint32_t ep_cfg = EP_CFG_ENABLE | EP_CFG_MAXPKTSIZE (FS_MPS);

  select_ep (0x00);
  if (dev.dev_ver >= DEV_VER_V3)
    {
      set_bits (DTRANS, (1u << 0) | (1u << 16));
      set_bits (TDL_FROM_TRB, (1u << 0) | (1u << 16));
    }
  wr (EP_CFG, ep_cfg);
  wr (EP_STS_EN, EP_STS_EN_SETUPEN | EP_STS_EN_DESCMISEN | EP_STS_EN_TRBERREN);

  select_ep (0x80);
  wr (EP_CFG, ep_cfg);
  wr (EP_STS_EN, EP_STS_EN_SETUPEN | EP_STS_EN_TRBERREN);

  set_bits (USB_CONF, USB_CONF_U1DS | USB_CONF_U2DS);
  dev.setup_armed = false;
  dev.wait_for_setup = false;
}

static void
ep0_interrupt (uint8_t dir)
{
  select_ep (dir);
  const uint32_t sts = rd (EP_STS);
  wr (EP_STS, sts);

  if (sts & EP_STS_SETUP)
    {
      dev.wait_for_setup = true;
    }

  if (dev.wait_for_setup && (sts & EP_STS_IOC))
    {
      dev.wait_for_setup = false;
      dev.setup_armed = false;
      vf2_dcache_flush (setup_buf, 8);
      memcpy (&dev.setup, setup_buf, 8);
      dev.ep0_data_dir = dev.setup.bmRequestType & 0x80u;
      dev.ep0_data_done = false;
      dcd_event_setup_received (0, setup_buf, true);
    }
  else if (sts & (EP_STS_IOC | EP_STS_ISP))
    {
      // Data stage done (cdns3_transfer_completed): the actual length is
      // the TRB's.
      vf2_dcache_flush (ep0_trb, sizeof (ep0_trb));
      uint32_t actual = dev.ep0_len;
      if (dir == 0)
        {
          actual = TRB_LEN (ep0_trb[0].length);
          if (actual > dev.ep0_len)
            {
              actual = dev.ep0_len;
            }
          vf2_dcache_flush (dev.ep0_buf, dev.ep0_len);
        }
      dev.ep0_data_done = true;
      dcd_event_xfer_complete (0, dir ? 0x80 : 0x00, actual,
                               XFER_RESULT_SUCCESS, true);
    }

  if ((sts & EP_STS_DESCMIS) && dir == 0 && !dev.setup_armed)
    {
      prepare_setup_packet ();
    }
}

//--------------------------------------------------------------------+
// Data endpoints (cdns3-gadget.c)
//--------------------------------------------------------------------+
static inline ep_state_t*
ep_of (uint8_t ep_addr)
{
  return &eps[tu_edpt_number (ep_addr)][tu_edpt_dir (ep_addr)];
}

static inline trb_t*
ring_of (uint8_t ep_addr)
{
  return ep_rings[tu_edpt_number (ep_addr)][tu_edpt_dir (ep_addr)].ring;
}

static void
ring_init (uint8_t ep_addr)
{
  trb_t* r = ring_of (ep_addr);
  memset ((void*) r, 0, sizeof (trb_t) * RING_TRBS);
  r[RING_TRBS - 1].buffer = phys (r);
  r[RING_TRBS - 1].control = TRB_CYCLE | TRB_TYPE (TRB_LINK) | TRB_TOGGLE;
  vf2_dcache_flush (r, sizeof (trb_t) * RING_TRBS);
}

static void
start_xfer (uint8_t ep_addr)
{
  ep_state_t* ep = ep_of (ep_addr);
  trb_t* r = ring_of (ep_addr);
  trb_t* t = &r[ep->enqueue];

  select_ep (ep_addr);

  // The LINK TRB carries the producer cycle state when the ring wraps.
  if (ep->enqueue + 1u >= RING_TRBS - 1u)
    {
      r[RING_TRBS - 1].control = (ep->pcs ? TRB_CYCLE : 0)
                                 | TRB_TYPE (TRB_LINK) | TRB_TOGGLE
                                 | TRB_CHAIN;
    }

  vf2_dcache_flush (ep->buf, ep->len);

  const uint32_t td_size = (ep->len + ep->mps - 1u) / ep->mps;
  t->buffer = phys (ep->buf);
  t->length = TRB_BURST_LEN (16) | TRB_LEN (ep->len);
  __asm__ volatile("fence w, w" ::: "memory");
  t->control = (ep->pcs ? TRB_CYCLE : 0) | TRB_TYPE (TRB_NORMAL) | TRB_IOC
               | TRB_ISP | TRB_TDL_HS_SIZE (td_size);
  vf2_dcache_flush (r, sizeof (trb_t) * RING_TRBS);

  ep->started = ep->enqueue;
  ep->enqueue++;
  if (ep->enqueue == RING_TRBS - 1u)
    {
      ep->enqueue = 0;
      ep->pcs ^= 1u;
    }

  if (ep->update_traddr)
    {
      wr (EP_TRADDR, phys (&r[ep->started]));
      ep->update_traddr = false;
    }
  wr (EP_STS, EP_STS_TRBERR | EP_STS_DESCMIS);
  wr (EP_CMD, EP_CMD_DRDY);
  wakeup ();
}

static void
ep_interrupt (uint8_t ep_addr)
{
  ep_state_t* ep = ep_of (ep_addr);
  select_ep (ep_addr);
  const uint32_t sts = rd (EP_STS);
  wr (EP_STS, sts);

  if (!ep->busy || ep->deferred)
    {
      return;
    }
  if (sts & (EP_STS_IOC | EP_STS_ISP | EP_STS_IOT))
    {
      trb_t* t = &ring_of (ep_addr)[ep->started];
      vf2_dcache_flush (t, sizeof (*t));
      uint32_t actual = ep->len;
      if (tu_edpt_dir (ep_addr) == TUSB_DIR_OUT)
        {
          actual = TRB_LEN (t->length);
          if (actual > ep->len)
            {
              actual = ep->len;
            }
          vf2_dcache_flush (ep->buf, ep->len);
        }
      ep->busy = false;
      ep_done_count[tu_edpt_number (ep_addr)][tu_edpt_dir (ep_addr)]++;
      dcd_event_xfer_complete (0, ep_addr, actual, XFER_RESULT_SUCCESS,
                               true);
    }
}

static void
reset_eps_config (void)
{
  wr (USB_CONF, USB_CONF_CFGRST);
  dev.hw_configured = false;
  dev.onchip_used = 0;
  dev.out_mem = 0;
  dev.wait_for_setup = false;
  for (unsigned n = 1; n < 16; ++n)
    {
      eps[n][0].open = eps[n][1].open = false;
      eps[n][0].busy = eps[n][1].busy = false;
      eps[n][0].deferred = eps[n][1].deferred = false;
    }
}

static void
configure_dmult (uint8_t num, uint8_t dir, uint8_t type)
{
  if (dev.dev_ver < DEV_VER_V3)
    {
      return;
    }
  const uint32_t mask = dir ? (1u << (num + 16)) : (1u << num);
  if (type != TUSB_XFER_ISOCHRONOUS && !dir)
    {
      set_bits (TDL_FROM_TRB, mask);
      set_bits (TDL_BEH, mask);
      set_bits (TDL_BEH2, mask);
      set_bits (DMA_ADV_TD, mask);
    }
  set_bits (DTRANS, mask);
}

// Diagnostics for the tests: transfers completed per endpoint and whether
// one is queued now.
uint32_t
dcd_cdns3_ep_done (uint8_t ep_addr)
{
  return ep_done_count[tu_edpt_number (ep_addr)][tu_edpt_dir (ep_addr)];
}

bool
dcd_cdns3_ep_busy (uint8_t ep_addr)
{
  return ep_of (ep_addr)->busy;
}

//--------------------------------------------------------------------+
// TinyUSB DCD API
//--------------------------------------------------------------------+
bool
dcd_init (uint8_t rhport, const tusb_rhport_init_t* rh_init)
{
  (void) rhport;
  (void) rh_init;
  memset (&dev, 0, sizeof (dev));
  memset (eps, 0, sizeof (eps));
  dev.dev_ver = rd (USB_CAP6) & 0x00FFFFFFu;
  dev.onchip_kb = rd (USB_CAP2) & 0xFFu;
  if (dev.onchip_kb == 0)
    {
      dev.onchip_kb = 256;
    }

  // cdns3_gadget_udc_start, full speed.
  wr (USB_CONF, USB_CONF_SFORCE_FS);
  wr (USB_CONF, USB_CONF_USB3DIS);
  wr (EP_DMA_EXT_ADDR, 0);

  // cdns3_gadget_config.
  ep0_config ();
  wr (EP_IEN, (1u << 0) | (1u << 16));
  wr (DMA_AXI_CTRL, rd (DMA_AXI_CTRL) | DMA_AXI_CTRL_NON_SECURE);
  wr (USB_ISTS, 0xFFFFFFFFu);
  wr (USB_IEN, USB_IEN_INIT);
  wr (USB_CONF, USB_CONF_CLK2OFFDS | USB_CONF_L1DS);
  wr (USB_PWR, PUSB_PWR_FST_REG_ACCESS);

  dcd_connect (rhport);
  return true;
}

void
dcd_int_enable (uint8_t rhport)
{
  (void) rhport; // polled
}

void
dcd_int_disable (uint8_t rhport)
{
  (void) rhport;
}

void
dcd_int_handler (uint8_t rhport)
{
  (void) rhport;
  const uint32_t ists = rd (USB_ISTS);
  if (ists)
    {
      wr (USB_ISTS, ists);
      if (ists & (USB_ISTS_CON2I | USB_ISTS_CONI))
        {
          ep0_config ();
        }
      if (ists & (USB_ISTS_DIS2I | USB_ISTS_DISI))
        {
          reset_eps_config ();
          dcd_event_bus_signal (0, DCD_EVENT_UNPLUGGED, true);
        }
      if (ists & (USB_ISTS_U2RESI | USB_ISTS_UHRESI | USB_ISTS_UWRESI))
        {
          const uint32_t speed = USB_STS_SPEED (rd (USB_STS));
          reset_eps_config ();
          ep0_config ();
          dcd_event_bus_reset (0,
                               speed == 3 ? TUSB_SPEED_HIGH
                                          : TUSB_SPEED_FULL,
                               true);
        }
      if (ists & (USB_ISTS_L2ENTI | USB_ISTS_U3ENTI))
        {
          dcd_event_bus_signal (0, DCD_EVENT_SUSPEND, true);
        }
      if (ists & (USB_ISTS_L2EXTI | USB_ISTS_U3EXTI))
        {
          dcd_event_bus_signal (0, DCD_EVENT_RESUME, true);
        }
    }

  const uint32_t ep_ists = rd (EP_ISTS);
  if (ep_ists & (1u << 0))
    {
      ep0_interrupt (0x00);
    }
  if (ep_ists & (1u << 16))
    {
      ep0_interrupt (0x80);
    }
  for (unsigned bit = 1; bit < 32; ++bit)
    {
      if (bit == 16 || !(ep_ists & (1u << bit)))
        {
          continue;
        }
      const uint8_t num = (uint8_t) (bit & 0xFu);
      const uint8_t addr = (bit >= 16) ? (uint8_t) (0x80u | num) : num;
      ep_interrupt (addr);
    }
}

void
dcd_set_address (uint8_t rhport, uint8_t dev_addr)
{
  (void) rhport;
  wr (USB_CMD, rd (USB_CMD) | USB_CMD_FADDR (dev_addr) | USB_CMD_SET_ADDR);
  // The status stage is the controller's: TinyUSB queues none here.
  ep0_complete_setup (false, true);
}

void
dcd_remote_wakeup (uint8_t rhport)
{
  (void) rhport;
  wakeup ();
}

void
dcd_connect (uint8_t rhport)
{
  (void) rhport;
  wr (USB_CONF, USB_CONF_DEVEN);
}

void
dcd_disconnect (uint8_t rhport)
{
  (void) rhport;
  wr (USB_CONF, USB_CONF_DEVDS);
}

void
dcd_sof_enable (uint8_t rhport, bool en)
{
  (void) rhport;
  (void) en;
}

bool
dcd_edpt_open (uint8_t rhport, tusb_desc_endpoint_t const* desc)
{
  (void) rhport;
  const uint8_t addr = desc->bEndpointAddress;
  const uint8_t num = tu_edpt_number (addr);
  const uint8_t dir = tu_edpt_dir (addr);
  const uint8_t type = desc->bmAttributes.xfer;
  const uint16_t mps = tu_edpt_packet_size (desc);
  if (num == 0 || num > 15 || type == TUSB_XFER_ISOCHRONOUS)
    {
      return false;
    }

  // On-chip buffer: 2 x 1 KB per IN endpoint; the OUT endpoints share one
  // chunk; 2 KB are EP0's (cdns3_ep_onchip_buffer_reserve).
  const uint32_t buffering = 1u;
  const uint32_t need = buffering + 1u;
  if (!dev.hw_configured)
    {
      const uint32_t remained = dev.onchip_kb - dev.onchip_used - 2u;
      if (dir)
        {
          if (remained < need)
            {
              return false;
            }
          dev.onchip_used += need;
        }
      else if (dev.out_mem < need)
        {
          const uint32_t req = need - dev.out_mem;
          if (req > remained)
            {
              return false;
            }
          dev.out_mem += req;
          dev.onchip_used += req;
        }
    }

  configure_dmult (num, dir, type);

  uint32_t cfg = EP_CFG_EPTYPE (type) | EP_CFG_ENABLE
                 | EP_CFG_MAXPKTSIZE (FS_MPS) | EP_CFG_BUFFERING (buffering);
  if (!dir)
    {
      cfg |= EP_CFG_TDL_CHK;
    }
  select_ep (addr);
  wr (EP_CFG, cfg);

  // cdns3_gadget_ep_enable: EPRST, interrupt, status enables, ring.
  wr (EP_CMD, EP_CMD_EPRST);
  for (unsigned i = 0; i < 1000u && (rd (EP_CMD) & (EP_CMD_CSTALL | EP_CMD_EPRST));
       ++i)
    {
      vf2_usb_udelay (1);
    }
  set_bits (EP_IEN, 1u << (num + (dir ? 16u : 0u)));
  wr (EP_STS_EN, EP_STS_EN_TRBERREN);

  ep_state_t* ep = ep_of (addr);
  memset (ep, 0, sizeof (*ep));
  ep->open = true;
  ep->type = type;
  ep->mps = (mps < FS_MPS) ? mps : FS_MPS;
  ep->update_traddr = true;
  ep->pcs = (rd (EP_STS) & EP_STS_CCS) ? 1u : 0u;
  ring_init (addr);
  return true;
}

void
dcd_edpt_close_all (uint8_t rhport)
{
  (void) rhport;
  reset_eps_config ();
}

bool
dcd_edpt_xfer (uint8_t rhport, uint8_t ep_addr, uint8_t* buffer,
               uint16_t total_bytes, bool is_isr)
{
  (void) rhport;
  (void) is_isr;
  const uint8_t num = tu_edpt_number (ep_addr);
  const uint8_t dir = tu_edpt_dir (ep_addr);

  if (num == 0)
    {
      // A zero-length transfer in the STATUS direction is the status stage,
      // which the controller sends on REQ_CMPL.
      const bool no_data = (dev.setup.wLength == 0);
      const bool status
          = (total_bytes == 0)
            && (no_data ? (dir == TUSB_DIR_IN)
                        : ((dir ? 0x80u : 0u) != dev.ep0_data_dir));
      if (status)
        {
          const bool set_config
              = (dev.setup.bmRequestType == 0x00) && (dev.setup.bRequest == 9);
          if (set_config && dev.setup.wValue != 0)
            {
              // cdns3_gadget_ep0_queue, STATUS of SET_CONFIGURATION.
              wr (USB_CONF, USB_CONF_CFGSET);
              dev.hw_configured = true;
              ep0_complete_setup (false, true);
              for (unsigned i = 0; i < 100u && !(rd (USB_STS) & USB_STS_CFGSTS);
                   ++i)
                {
                  vf2_usb_udelay (1);
                }
              for (unsigned n = 1; n < 16; ++n)
                {
                  for (unsigned d = 0; d < 2; ++d)
                    {
                      if (eps[n][d].deferred)
                        {
                          eps[n][d].deferred = false;
                          start_xfer ((uint8_t) (n | (d ? 0x80u : 0u)));
                        }
                    }
                }
            }
          else
            {
              ep0_complete_setup (false, no_data);
            }
          dcd_event_xfer_complete (0, ep_addr, 0, XFER_RESULT_SUCCESS, false);
          return true;
        }
      dev.ep0_buf = buffer;
      dev.ep0_len = total_bytes;
      ep0_run_transfer (dir ? 0x80 : 0x00, buffer, total_bytes, true);
      return true;
    }

  ep_state_t* ep = ep_of (ep_addr);
  if (!ep->open || ep->busy)
    {
      return false;
    }
  ep->buf = buffer;
  ep->len = total_bytes;
  ep->busy = true;
  if (!dev.hw_configured)
    {
      ep->deferred = true; // started after CFGSET
      return true;
    }
  start_xfer (ep_addr);
  return true;
}

void
dcd_edpt_stall (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;
  if (tu_edpt_number (ep_addr) == 0)
    {
      ep0_complete_setup (true, true);
      return;
    }
  select_ep (ep_addr);
  wr (EP_CMD, EP_CMD_SSTALL);
}

void
dcd_edpt_clear_stall (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;
  if (tu_edpt_number (ep_addr) == 0)
    {
      return;
    }
  select_ep (ep_addr);
  wr (EP_CMD, EP_CMD_CSTALL | EP_CMD_EPRST);
  for (unsigned i = 0; i < 100u && (rd (EP_CMD) & EP_CMD_EPRST); ++i)
    {
      vf2_usb_udelay (1);
    }
  ep_state_t* ep = ep_of (ep_addr);
  ep->enqueue = 0;
  ep->busy = false;
  ep->update_traddr = true;
  ep->pcs = (rd (EP_STS) & EP_STS_CCS) ? 1u : 0u;
  ring_init (ep_addr);
}

void
dcd_edpt_close (uint8_t rhport, uint8_t ep_addr)
{
  (void) rhport;
  ep_state_t* ep = ep_of (ep_addr);
  ep->open = false;
  ep->busy = false;
}

#endif // CFG_TUD_ENABLED && CFG_TUSB_MCU == OPT_MCU_JH7110
