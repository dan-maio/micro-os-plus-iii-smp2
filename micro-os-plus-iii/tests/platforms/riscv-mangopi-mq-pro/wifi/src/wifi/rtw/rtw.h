/* rtw.h - bare-metal RTL8723DS driver, ported from Linux rtw88.
 *
 * Only what one 2.4 GHz station needs is kept: SDIO register access, MAC
 * power-on, firmware download, MAC/BB/RF configuration, channel switching,
 * TX power, calibration, and the TX/RX descriptors.  The mac80211 glue,
 * power saving, Bluetooth coexistence state machine and rate adaptation
 * bookkeeping of rtw88 are not part of this port.
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Based on rtw88: Copyright(c) 2018-2019 Realtek Corporation and others.
 */
#ifndef MQPRO_RTW_H
#define MQPRO_RTW_H

#include "rtw_osdep.h"
#include "rtw_reg.h"

/* ------------------------------------------------------------ constants */
#define RFREG_MASK		0xfffff
#define INV_RF_DATA		0xffffffff
#define MASKBYTE0		0xff
#define MASKLWORD		0x0000ffff
#define MASKDWORD		0xffffffff
#define LSSI_READ_ADDR_MASK	0x7f800000
#define LSSI_READ_EDGE_MASK	0x80000000
#define LSSI_READ_DATA_MASK	0xfffff

#define TX_PAGE_SIZE_SHIFT	7
#define TX_PAGE_SIZE		(1 << TX_PAGE_SIZE_SHIFT)
#define REPORT_BUF		128
#define PHY_STATUS_SIZE		4

#define RTW_TX_DESC_SIZE	40
#define RTW_RX_DESC_SIZE	24
#define RTW_TXFF_SIZE		32768
#define RTW_RXFF_SIZE		16384
#define RTW_RSVD_DRV_PG_NUM	8
#define RTW_MAX_POWER_INDEX	0x3f
#define RTW_EFUSE_PHYS_SIZE	512
#define RTW_EFUSE_LOG_SIZE	512
#define RTW_EFUSE_PROTECT_SIZE	(96 + 1)

enum rtw_rf_path { RF_PATH_A = 0, RF_PATH_B = 1 };

enum rtw_bandwidth {
	RTW_CHANNEL_WIDTH_20 = 0,
	RTW_CHANNEL_WIDTH_40 = 1,
};

enum rtw_trx_desc_rate {
	DESC_RATE1M = 0x00, DESC_RATE2M, DESC_RATE5_5M, DESC_RATE11M,
	DESC_RATE6M = 0x04, DESC_RATE9M, DESC_RATE12M, DESC_RATE18M,
	DESC_RATE24M, DESC_RATE36M, DESC_RATE48M, DESC_RATE54M,
	DESC_RATEMCS0 = 0x0c, DESC_RATEMCS1, DESC_RATEMCS2, DESC_RATEMCS3,
	DESC_RATEMCS4, DESC_RATEMCS5, DESC_RATEMCS6, DESC_RATEMCS7,
	DESC_RATE_NUM
};

enum rtw_tx_queue {
	RTW_QUEUE_DATA,		/* BE, low FIFO */
	RTW_QUEUE_MGMT,		/* management, extra FIFO */
	RTW_QUEUE_H2C,		/* firmware command packets, high FIFO */
};

/* efuse TX power index layout, exactly as stored in the chip */
struct rtw_2g_1s_pwr_idx_diff {
	s8 ofdm:4;
	s8 bw20:4;
} __packed;

struct rtw_2g_ns_pwr_idx_diff {
	s8 bw20:4;
	s8 bw40:4;
	s8 cck:4;
	s8 ofdm:4;
} __packed;

struct rtw_2g_txpwr_idx {
	u8 cck_base[6];
	u8 bw40_base[5];
	struct rtw_2g_1s_pwr_idx_diff ht_1s_diff;
	struct rtw_2g_ns_pwr_idx_diff ht_2s_diff;
	struct rtw_2g_ns_pwr_idx_diff ht_3s_diff;
	struct rtw_2g_ns_pwr_idx_diff ht_4s_diff;
};

struct rtw_5g_txpwr_idx {
	u8 raw[24];
} __packed;

struct rtw_txpwr_idx {
	struct rtw_2g_txpwr_idx pwr_idx_2g;
	struct rtw_5g_txpwr_idx pwr_idx_5g;
} __packed;

struct rtw_efuse {
	u8 addr[ETH_ALEN];
	u8 channel_plan;
	u8 country_code[2];
	u8 rf_board_option;
	u8 rfe_option;
	u8 crystal_cap;
	u8 pa_type_2g;
	u8 lna_type_2g;
	u8 bt_setting;
	u8 regd;
	u8 thermal_meter;
	u8 afe;
	bool share_ant;
	bool btcoex;
	struct rtw_txpwr_idx txpwr_idx_table[4];
};

struct rtw_rx_info {
	u16 pkt_len;		/* 802.11 frame length, FCS excluded */
	u8 drv_info_sz;
	u8 shift;
	u8 rate;
	bool phy_status;
	bool crc_err;
	bool icv_err;
	bool decrypted;
	bool is_c2h;
	s8 rssi_dbm;
};

struct rtw_dev {
	bool powered;		/* MAC is on: CMD53 register access allowed */
	bool fw_running;
	u32 chip_version;
	u8 cut_version;
	u16 fw_version;
	u8 fw_sub_version;

	struct rtw_efuse efuse;
	u8 efuse_log[RTW_EFUSE_LOG_SIZE];

	u8 channel;
	u8 tx_pwr_tbl[DESC_RATE_NUM];
	u8 cck_pd_default;

	u16 rsvd_boundary;
	u16 acq_pg_num;
	u8 h2c_box;
	u8 rx_addr;

	bool iqk_done;
	u32 tx_count, tx_busy, rx_count, rx_errors;
	u32 tx_dma_errors, tx_dma_status;
	u32 rcr;
};

extern struct rtw_dev rtw;

/* ----------------------------------------------------------- rtw_io.c */
u8 rtw_read8(struct rtw_dev *rtwdev, u32 addr);
u16 rtw_read16(struct rtw_dev *rtwdev, u32 addr);
u32 rtw_read32(struct rtw_dev *rtwdev, u32 addr);
void rtw_write8(struct rtw_dev *rtwdev, u32 addr, u8 val);
void rtw_write16(struct rtw_dev *rtwdev, u32 addr, u16 val);
void rtw_write32(struct rtw_dev *rtwdev, u32 addr, u32 val);
extern u32 rtw_io_errors;

static inline void rtw_write8_set(struct rtw_dev *d, u32 addr, u8 bit)
{
	rtw_write8(d, addr, rtw_read8(d, addr) | bit);
}
static inline void rtw_write16_set(struct rtw_dev *d, u32 addr, u16 bit)
{
	rtw_write16(d, addr, rtw_read16(d, addr) | bit);
}
static inline void rtw_write32_set(struct rtw_dev *d, u32 addr, u32 bit)
{
	rtw_write32(d, addr, rtw_read32(d, addr) | bit);
}
static inline void rtw_write8_clr(struct rtw_dev *d, u32 addr, u8 bit)
{
	rtw_write8(d, addr, rtw_read8(d, addr) & ~bit);
}
static inline void rtw_write16_clr(struct rtw_dev *d, u32 addr, u16 bit)
{
	rtw_write16(d, addr, rtw_read16(d, addr) & ~bit);
}
static inline void rtw_write32_clr(struct rtw_dev *d, u32 addr, u32 bit)
{
	rtw_write32(d, addr, rtw_read32(d, addr) & ~bit);
}
static inline u32 rtw_read32_mask(struct rtw_dev *d, u32 addr, u32 mask)
{
	return (rtw_read32(d, addr) & mask) >> __ffs(mask);
}
static inline u8 rtw_read8_mask(struct rtw_dev *d, u32 addr, u32 mask)
{
	return (rtw_read8(d, addr) & mask) >> __ffs(mask);
}
static inline void rtw_write32_mask(struct rtw_dev *d, u32 addr, u32 mask, u32 data)
{
	u32 orig = rtw_read32(d, addr);

	rtw_write32(d, addr, (orig & ~mask) | ((data << __ffs(mask)) & mask));
}
static inline void rtw_write8_mask(struct rtw_dev *d, u32 addr, u32 mask, u8 data)
{
	u8 orig;

	mask &= 0xff;
	orig = rtw_read8(d, addr);
	rtw_write8(d, addr, (orig & ~mask) | ((data << __ffs(mask)) & mask));
}

bool check_hw_ready(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target);

/* ---------------------------------------------------------- rtw_mac.c */
int rtw_mac_power_on(struct rtw_dev *rtwdev);
void rtw_mac_power_off(struct rtw_dev *rtwdev);
int rtw_download_firmware(struct rtw_dev *rtwdev, const u8 *data, u32 size);
int rtw_mac_init(struct rtw_dev *rtwdev);
int rtw_parse_efuse_map(struct rtw_dev *rtwdev);
void rtw_set_channel_mac(struct rtw_dev *rtwdev, u8 channel, u8 bw);

/* ---------------------------------------------------------- rtw_phy.c */
u32 rtw_read_rf(struct rtw_dev *rtwdev, enum rtw_rf_path path, u32 addr, u32 mask);
bool rtw_write_rf(struct rtw_dev *rtwdev, enum rtw_rf_path path, u32 addr,
		  u32 mask, u32 data);
void rtw8723d_phy_set_param(struct rtw_dev *rtwdev);
void rtw8723d_set_channel(struct rtw_dev *rtwdev, u8 channel);
void rtw_phy_set_tx_power_level(struct rtw_dev *rtwdev, u8 channel);
void rtw8723d_phy_calibration(struct rtw_dev *rtwdev);
void rtw_coex_wifi_only(struct rtw_dev *rtwdev);
void rtw_phy_dig_write(struct rtw_dev *rtwdev, u8 igi);

/* ---------------------------------------------------------- rtw_trx.c */
/* Build a TX descriptor in front of an 802.11 frame.  `buf` holds
 * RTW_TX_DESC_SIZE bytes of headroom followed by `len` frame bytes. */
struct rtw_tx_info {
	enum rtw_tx_queue queue;
	u8 rate;		/* DESC_RATE* used for this frame */
	u8 mac_id;
	bool bmc;		/* group addressed: no ACK expected */
	bool hw_seq;		/* let the MAC assign the sequence number */
	bool fixed_rate;	/* send at `rate` only; else firmware RA */
};
int rtw_tx_frame(struct rtw_dev *rtwdev, u8 *buf, u32 len,
		 const struct rtw_tx_info *info);
/* Poll the RX FIFO; returns the number of bytes read into `buf` (a whole
 * aggregate of descriptors + frames) or 0. */
int rtw_rx_poll(struct rtw_dev *rtwdev, u8 *buf, u32 buf_size);
/* Parse one descriptor of an aggregate.  Returns the total size consumed by
 * this entry (8-byte aligned) or 0 when the buffer is exhausted/corrupt. */
u32 rtw_rx_parse(struct rtw_dev *rtwdev, const u8 *desc, u32 remaining,
		 struct rtw_rx_info *info, const u8 **frame);
int rtw_fw_send_h2c(struct rtw_dev *rtwdev, const u8 *h2c /* 8 bytes */);
void rtw_fw_media_status(struct rtw_dev *rtwdev, u8 mac_id, bool connect);
void rtw_fw_send_ra_info(struct rtw_dev *rtwdev, u8 mac_id, u32 rate_mask,
			 u8 rssi_level);
void rtw_fw_send_rssi_info(struct rtw_dev *rtwdev, u8 mac_id, u8 rssi);
void rtw_hci_start(struct rtw_dev *rtwdev);

/* --------------------------------------------------------- rtw_core.c */
int rtw_core_init(void);		/* power on, firmware, MAC/BB/RF */
void rtw_set_mac_addr(struct rtw_dev *rtwdev, const u8 *addr);
void rtw_set_bssid(struct rtw_dev *rtwdev, const u8 *bssid);
void rtw_set_linked(struct rtw_dev *rtwdev, bool linked, u16 aid,
		    u16 beacon_interval);
void rtw_set_rx_filter(struct rtw_dev *rtwdev, bool bss_only);
void rtw_core_shutdown(void);

/* firmware image linked into the binary (rtw_fw.S) */
extern const u8 rtw8723d_fw_start[];
extern const u8 rtw8723d_fw_end[];

#endif
