/* rtw_phy.c - RTL8723D baseband/RF: 3-wire RF access, parameter tables,
 * PHY bring-up, channel switching, TX power, LC calibration and the
 * WLAN-only antenna setup.  Ported from Linux rtw88 (phy.c, rtw8723d.c,
 * rtw8723x.c, coex.c).
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation; Copyright 2024 Fiona Klute
 */
#include "rtw_8723x.h"
#include "rtw_phy.h"

/* ---------------------------------------------------- RF (3-wire) access */
struct rtw_rf_sipi_addr {
	u32 hssi_1;
	u32 hssi_2;
	u32 lssi_read;
	u32 lssi_read_pi;
};

static const struct rtw_rf_sipi_addr rf_sipi_read_addr[2] = {
	[RF_PATH_A] = { .hssi_1 = 0x820, .lssi_read    = 0x8a0,
			.hssi_2 = 0x824, .lssi_read_pi = 0x8b8},
	[RF_PATH_B] = { .hssi_1 = 0x828, .lssi_read    = 0x8a4,
			.hssi_2 = 0x82c, .lssi_read_pi = 0x8bc},
};
static const u32 rf_sipi_addr[2] = {0x840, 0x844};

u32 rtw_read_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
		u32 addr, u32 mask)
{
	const struct rtw_rf_sipi_addr *sipi = &rf_sipi_read_addr[rf_path];
	const struct rtw_rf_sipi_addr *sipi_a = &rf_sipi_read_addr[RF_PATH_A];
	u32 val32;
	u32 en_pi;
	u32 r_addr;

	addr &= 0xff;

	val32 = rtw_read32(rtwdev, sipi->hssi_2);
	val32 = (val32 & ~LSSI_READ_ADDR_MASK) | (addr << 23);
	rtw_write32(rtwdev, sipi->hssi_2, val32);

	/* toggle read edge of path A */
	val32 = rtw_read32(rtwdev, sipi_a->hssi_2);
	rtw_write32(rtwdev, sipi_a->hssi_2, val32 & ~LSSI_READ_EDGE_MASK);
	rtw_write32(rtwdev, sipi_a->hssi_2, val32 | LSSI_READ_EDGE_MASK);

	udelay(120);

	en_pi = rtw_read32_mask(rtwdev, sipi->hssi_1, BIT(8));
	r_addr = en_pi ? sipi->lssi_read_pi : sipi->lssi_read;

	val32 = rtw_read32_mask(rtwdev, r_addr, LSSI_READ_DATA_MASK);

	return (val32 & mask) >> __ffs(mask);
}

bool rtw_write_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
		  u32 addr, u32 mask, u32 data)
{
	u32 data_and_addr;
	u32 old_data;

	addr &= 0xff;
	mask &= RFREG_MASK;

	if (mask != RFREG_MASK) {
		old_data = rtw_read_rf(rtwdev, rf_path, addr, RFREG_MASK);
		data = ((old_data) & (~mask)) | (data << __ffs(mask));
	}

	data_and_addr = ((addr << 20) | (data & 0x000fffff)) & 0x0fffffff;

	rtw_write32(rtwdev, rf_sipi_addr[rf_path], data_and_addr);

	udelay(13);

	return true;
}

/* ------------------------------------------------------ parameter tables */
struct rtw_phy_cond {
	u32 rfe:8;
	u32 intf:4;
	u32 pkg:4;
	u32 plat:4;
	u32 intf_rsvd:4;
	u32 cut:4;
	u32 branch:2;
	u32 neg:1;
	u32 pos:1;
	#define INTF_PCIE	BIT(0)
	#define INTF_USB	BIT(1)
	#define INTF_SDIO	BIT(2)
	#define BRANCH_IF	0
	#define BRANCH_ELIF	1
	#define BRANCH_ELSE	2
	#define BRANCH_ENDIF	3
};

struct phy_cfg_pair {
	u32 addr;
	u32 data;
};

union phy_table_tile {
	struct rtw_phy_cond cond;
	struct phy_cfg_pair cfg;
};

static bool check_positive(struct rtw_dev *rtwdev, struct rtw_phy_cond cond)
{
	struct rtw_phy_cond drv_cond = {0};

	drv_cond.cut = rtwdev->cut_version ? rtwdev->cut_version : 15;
	drv_cond.pkg = 15;
	drv_cond.plat = 0x04;
	drv_cond.rfe = rtwdev->efuse.rfe_option;
	drv_cond.intf = INTF_SDIO;

	if (cond.cut && cond.cut != drv_cond.cut)
		return false;

	if (cond.pkg && cond.pkg != drv_cond.pkg)
		return false;

	if (cond.intf && cond.intf != drv_cond.intf)
		return false;

	if (cond.rfe != drv_cond.rfe)
		return false;

	return true;
}

static void rtw_load_table(struct rtw_dev *rtwdev, const struct rtw_table *tbl)
{
	const union phy_table_tile *p = tbl->data;
	const union phy_table_tile *end = p + tbl->size / 2;
	struct rtw_phy_cond pos_cond = {0};
	bool is_matched = true, is_skipped = false;

	for (; p < end; p++) {
		if (p->cond.pos) {
			switch (p->cond.branch) {
			case BRANCH_ENDIF:
				is_matched = true;
				is_skipped = false;
				break;
			case BRANCH_ELSE:
				is_matched = is_skipped ? false : true;
				break;
			case BRANCH_IF:
			case BRANCH_ELIF:
			default:
				pos_cond = p->cond;
				break;
			}
		} else if (p->cond.neg) {
			if (!is_skipped) {
				if (check_positive(rtwdev, pos_cond)) {
					is_matched = true;
					is_skipped = true;
				} else {
					is_matched = false;
					is_skipped = false;
				}
			} else {
				is_matched = false;
			}
		} else if (is_matched) {
			(*tbl->do_cfg)(rtwdev, tbl, p->cfg.addr, p->cfg.data);
		}
	}
}

void rtw_phy_cfg_mac(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data)
{
	(void)tbl;
	rtw_write8(rtwdev, addr, data);
}

void rtw_phy_cfg_agc(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data)
{
	(void)tbl;
	rtw_write32(rtwdev, addr, data);
}

void rtw_phy_cfg_bb(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data)
{
	(void)tbl;
	if (addr == 0xfe)
		msleep(50);
	else if (addr == 0xfd)
		mdelay(5);
	else if (addr == 0xfc)
		mdelay(1);
	else if (addr == 0xfb)
		usleep_range(50, 60);
	else if (addr == 0xfa)
		udelay(5);
	else if (addr == 0xf9)
		udelay(1);
	else
		rtw_write32(rtwdev, addr, data);
}

void rtw_phy_cfg_rf(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data)
{
	if (addr == 0xffe) {
		msleep(50);
	} else if (addr == 0xfe) {
		usleep_range(100, 110);
	} else {
		rtw_write_rf(rtwdev, tbl->rf_path, addr, RFREG_MASK, data);
		udelay(1);
	}
}

static void rtw_phy_load_tables(struct rtw_dev *rtwdev)
{
	rtw_load_table(rtwdev, &rtw8723d_mac_tbl);
	rtw_load_table(rtwdev, &rtw8723d_bb_tbl);
	rtw_load_table(rtwdev, &rtw8723d_agc_tbl);
	rtw_load_table(rtwdev, &rtw8723d_rf_a_tbl);
}

void rtw_phy_dig_write(struct rtw_dev *rtwdev, u8 igi)
{
	rtw_write32_mask(rtwdev, 0xa0c, 0x3f00, igi >> 1);	/* CCK */
	rtw_write32_mask(rtwdev, 0xc50, 0x7f, igi);		/* OFDM */
}

/* --------------------------------------------------------- LC calibration */
static void rtw8723x_lck(struct rtw_dev *rtwdev)
{
	u32 lc_cal;
	u8 val_ctx, rf_val;
	int ret;

	val_ctx = rtw_read8(rtwdev, REG_CTX);
	if ((val_ctx & BIT_MASK_CTX_TYPE) != 0)
		rtw_write8(rtwdev, REG_CTX, val_ctx & ~BIT_MASK_CTX_TYPE);
	else
		rtw_write8(rtwdev, REG_TXPAUSE, 0xFF);
	lc_cal = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal | BIT_LCK);

	ret = -ETIMEDOUT;
	for (int i = 0; i < 100; i++) {
		rf_val = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, BIT_LCK);
		if (rf_val != 0x1) {
			ret = 0;
			break;
		}
		mdelay(10);
	}
	if (ret)
		rtw_warn(rtwdev, "failed to poll LCK status bit\n");

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal);
	if ((val_ctx & BIT_MASK_CTX_TYPE) != 0)
		rtw_write8(rtwdev, REG_CTX, val_ctx);
	else
		rtw_write8(rtwdev, REG_TXPAUSE, 0x00);
}

/* ------------------------------------------------------------ PHY bring-up */
#define WLAN_SLOT_TIME		0x09
#define WLAN_RL_VAL		0x3030
#define WLAN_BAR_VAL		0x0201ffff
#define BIT_MASK_TBTT_HOLD	0x00000fff
#define BIT_SHIFT_TBTT_HOLD	8
#define BIT_MASK_TBTT_SETUP	0x000000ff
#define BIT_SHIFT_TBTT_SETUP	0
#define BIT_MASK_TBTT_MASK	((BIT_MASK_TBTT_HOLD << BIT_SHIFT_TBTT_HOLD) | \
				 (BIT_MASK_TBTT_SETUP << BIT_SHIFT_TBTT_SETUP))
#define TBTT_TIME(s, h)((((s) & BIT_MASK_TBTT_SETUP) << BIT_SHIFT_TBTT_SETUP) |\
			(((h) & BIT_MASK_TBTT_HOLD) << BIT_SHIFT_TBTT_HOLD))
#define WLAN_TBTT_TIME_NORMAL	TBTT_TIME(0x04, 0x80)
#define WLAN_TBTT_TIME_STOP_BCN	TBTT_TIME(0x04, 0x64)
#define WLAN_PIFS_VAL		0
#define WLAN_AGG_BRK_TIME	0x16
#define WLAN_NAV_PROT_LEN	0x0040
#define WLAN_SPEC_SIFS		0x100a
#define WLAN_RX_PKT_LIMIT	0x17
#define WLAN_MAX_AGG_NR		0x0A
#define WLAN_AMPDU_MAX_TIME	0x1C
#define WLAN_ANT_SEL		0x82
#define WLAN_LTR_IDLE_LAT	0x90039003
#define WLAN_LTR_ACT_LAT	0x883c883c
#define WLAN_LTR_CTRL1		0xCB004010
#define WLAN_LTR_CTRL2		0x01233425

void rtw8723d_phy_set_param(struct rtw_dev *rtwdev)
{
	u8 xtal_cap;
	u32 val32;

	/* power on BB/RF domain */
	rtw_write16_set(rtwdev, REG_SYS_FUNC_EN,
			BIT_FEN_EN_25_1 | BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);
	rtw_write8_set(rtwdev, REG_RF_CTRL,
		       BIT_RF_EN | BIT_RF_RSTB | BIT_RF_SDM_RSTB);
	rtw_write8(rtwdev, REG_AFE_CTRL1 + 1, 0x80);

	rtw_phy_load_tables(rtwdev);

	/* post init after header files config */
	rtw_write32_clr(rtwdev, REG_RCR, BIT_RCR_ADF);
	rtw_write8_set(rtwdev, REG_HIQ_NO_LMT_EN, BIT_HIQ_NO_LMT_EN_ROOT);
	rtw_write16_set(rtwdev, REG_AFE_CTRL_4, BIT_CK320M_AFE_EN | BIT_EN_SYN);

	xtal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
	rtw_write32_set(rtwdev, REG_FPGA0_RFMOD, BIT_CCKEN | BIT_OFDMEN);
	if ((rtwdev->efuse.afe >> 4) == 14) {
		rtw_write32_set(rtwdev, REG_AFE_CTRL3, BIT_XTAL_GMP_BIT4);
		rtw_write32_clr(rtwdev, REG_AFE_CTRL1, BITS_PLL);
		rtw_write32_set(rtwdev, REG_LDO_SWR_CTRL, BIT_XTA1);
		rtw_write32_clr(rtwdev, REG_LDO_SWR_CTRL, BIT_XTA0);
	}

	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	rtw_write16(rtwdev, REG_RETRY_LIMIT, WLAN_RL_VAL);
	rtw_write32(rtwdev, REG_BAR_MODE_CTRL, WLAN_BAR_VAL);
	rtw_write8(rtwdev, REG_ATIMWND, 0x2);
	rtw_write8(rtwdev, REG_BCN_CTRL,
		   BIT_DIS_TSF_UDT | BIT_EN_BCN_FUNCTION | BIT_EN_TXBCN_RPT);
	val32 = rtw_read32(rtwdev, REG_TBTT_PROHIBIT);
	val32 &= ~BIT_MASK_TBTT_MASK;
	val32 |= WLAN_TBTT_TIME_STOP_BCN;
	rtw_write8(rtwdev, REG_TBTT_PROHIBIT, val32);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_VAL);
	rtw_write8(rtwdev, REG_AGGR_BREAK_TIME, WLAN_AGG_BRK_TIME);
	rtw_write16(rtwdev, REG_NAV_PROT_LEN, WLAN_NAV_PROT_LEN);
	rtw_write16(rtwdev, REG_MAC_SPEC_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS, WLAN_SPEC_SIFS);
	rtw_write16(rtwdev, REG_SIFS + 2, WLAN_SPEC_SIFS);
	rtw_write8(rtwdev, REG_SINGLE_AMPDU_CTRL, BIT_EN_SINGLE_APMDU);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RX_PKT_LIMIT);
	rtw_write8(rtwdev, REG_MAX_AGGR_NUM, WLAN_MAX_AGG_NR);
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME, WLAN_AMPDU_MAX_TIME);
	rtw_write8(rtwdev, REG_LEDCFG2, WLAN_ANT_SEL);

	rtw_write32(rtwdev, REG_LTR_IDLE_LATENCY, WLAN_LTR_IDLE_LAT);
	rtw_write32(rtwdev, REG_LTR_ACTIVE_LATENCY, WLAN_LTR_ACT_LAT);
	rtw_write32(rtwdev, REG_LTR_CTRL_BASIC, WLAN_LTR_CTRL1);
	rtw_write32(rtwdev, REG_LTR_CTRL_BASIC + 4, WLAN_LTR_CTRL2);

	rtwdev->cck_pd_default = rtw_read8(rtwdev, REG_CSRATIO) & 0x1f;

	rtw_write16_set(rtwdev, REG_TXDMA_OFFSET_CHK, BIT_DROP_DATA_EN);

	rtw8723x_lck(rtwdev);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x20);

}

/* --------------------------------------------------------- channel switch */
static bool rtw8723d_check_spur_ov_thres(struct rtw_dev *rtwdev,
					 u8 channel, u32 thres)
{
	u32 freq;
	bool ret = false;

	if (channel == 13)
		freq = FREQ_CH13;
	else if (channel == 14)
		freq = FREQ_CH14;
	else
		return false;

	rtw_write32(rtwdev, REG_ANALOG_P4, DIS_3WIRE);
	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_PSDFN, START_PSD | freq);

	msleep(30);
	if (rtw_read32(rtwdev, REG_PSDRPT) >= thres)
		ret = true;

	rtw_write32(rtwdev, REG_PSDFN, freq);
	rtw_write32(rtwdev, REG_ANALOG_P4, EN_3WIRE);

	return ret;
}

static void rtw8723d_cfg_notch(struct rtw_dev *rtwdev, u8 channel, bool notch)
{
	if (!notch) {
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x1f);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		return;
	}

	switch (channel) {
	case 13:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0xb);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x04000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00000000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	case 14:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_MASK_RXDSP, 0x5);
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x1);
		rtw_write32(rtwdev, REG_OFDM1_CSI1, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI2, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI3, 0x00000000);
		rtw_write32(rtwdev, REG_OFDM1_CSI4, 0x00080000);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x1);
		break;
	default:
		rtw_write32_mask(rtwdev, REG_OFDM0_RXDSP, BIT_EN_RXDSP, 0x0);
		rtw_write32_mask(rtwdev, REG_OFDM1_CFOTRK, BIT_EN_CFOTRK, 0x0);
		break;
	}
}

static void rtw8723d_spur_cal(struct rtw_dev *rtwdev, u8 channel)
{
	bool notch;

	if (channel < 13) {
		rtw8723d_cfg_notch(rtwdev, channel, false);
		return;
	}

	notch = rtw8723d_check_spur_ov_thres(rtwdev, channel, SPUR_THRES);
	rtw8723d_cfg_notch(rtwdev, channel, notch);
}

static void rtw8723d_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	u32 rf_cfgch_a, rf_cfgch_b;

	rf_cfgch_a = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);
	rf_cfgch_b = rtw_read_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK);

	rf_cfgch_a &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch_b &= ~RFCFGCH_CHANNEL_MASK;
	rf_cfgch_a |= (channel & RFCFGCH_CHANNEL_MASK);
	rf_cfgch_b |= (channel & RFCFGCH_CHANNEL_MASK);

	rf_cfgch_a &= ~RFCFGCH_BW_MASK;
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rf_cfgch_a |= RFCFGCH_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_cfgch_a |= RFCFGCH_BW_40M;
		break;
	default:
		break;
	}

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, rf_cfgch_a);
	rtw_write_rf(rtwdev, RF_PATH_B, RF_CFGCH, RFREG_MASK, rf_cfgch_b);

	rtw8723d_spur_cal(rtwdev, channel);
}

struct rtw_backup_info {
	u8 len;
	u32 reg;
	u32 val;
};

#define CCK_DFIR_NR		3
#define RTW_SC_20_UPPER		1

static const struct rtw_backup_info cck_dfir_cfg[][CCK_DFIR_NR] = {
	[0] = {
		{ .len = 4, .reg = 0xA24, .val = 0x64B80C1C },
		{ .len = 4, .reg = 0xA28, .val = 0x00008810 },
		{ .len = 4, .reg = 0xAAC, .val = 0x01235667 },
	},
	[1] = {
		{ .len = 4, .reg = 0xA24, .val = 0x0000B81C },
		{ .len = 4, .reg = 0xA28, .val = 0x00000000 },
		{ .len = 4, .reg = 0xAAC, .val = 0x00003667 },
	},
};

static void rtw8723d_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	const struct rtw_backup_info *cck_dfir;
	int i;

	cck_dfir = channel <= 13 ? cck_dfir_cfg[0] : cck_dfir_cfg[1];

	for (i = 0; i < CCK_DFIR_NR; i++, cck_dfir++)
		rtw_write32(rtwdev, cck_dfir->reg, cck_dfir->val);

	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x0);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_RXBB_DFIR_EN, 1);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_MASK_RXBB_DFIR, 0xa);
		break;
	case RTW_CHANNEL_WIDTH_40:
		rtw_write32_mask(rtwdev, REG_FPGA0_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_FPGA1_RFMOD, BIT_MASK_RFMOD, 0x1);
		rtw_write32_mask(rtwdev, REG_BBRX_DFIR, BIT_RXBB_DFIR_EN, 0);
		rtw_write32_mask(rtwdev, REG_CCK0_SYS, BIT_CCK_SIDE_BAND,
				 (primary_ch_idx == RTW_SC_20_UPPER ? 1 : 0));
		break;
	default:
		break;
	}
}

void rtw8723d_set_channel(struct rtw_dev *rtwdev, u8 channel)
{
	u8 bw = RTW_CHANNEL_WIDTH_20;

	rtw8723d_set_channel_rf(rtwdev, channel, bw);
	rtw_set_channel_mac(rtwdev, channel, bw);
	rtw8723d_set_channel_bb(rtwdev, channel, bw, 0);
	rtwdev->channel = channel;
	rtw_phy_set_tx_power_level(rtwdev, channel);
}

/* --------------------------------------------------------------- TX power */
/* index = efuse base for the channel group
 *         + min(by-rate offset, regulatory limit - section base).
 * The limit used is the lowest one of all regulatory domains in the table
 * ("worldwide"), since no country is configured on this system. */
static const u8 rtw_cck_rates[] = {
	DESC_RATE1M, DESC_RATE2M, DESC_RATE5_5M, DESC_RATE11M
};
static const u8 rtw_ofdm_rates[] = {
	DESC_RATE6M, DESC_RATE9M, DESC_RATE12M, DESC_RATE18M,
	DESC_RATE24M, DESC_RATE36M, DESC_RATE48M, DESC_RATE54M
};
static const u8 rtw_ht_1s_rates[] = {
	DESC_RATEMCS0, DESC_RATEMCS1, DESC_RATEMCS2, DESC_RATEMCS3,
	DESC_RATEMCS4, DESC_RATEMCS5, DESC_RATEMCS6, DESC_RATEMCS7
};

enum { RS_CCK = 0, RS_OFDM = 1, RS_HT_1S = 2, RS_NUM = 3 };

static const u8 * const rtw_rate_section[RS_NUM] = {
	rtw_cck_rates, rtw_ofdm_rates, rtw_ht_1s_rates
};
static const u8 rtw_rate_size[RS_NUM] = {
	ARRAY_SIZE(rtw_cck_rates), ARRAY_SIZE(rtw_ofdm_rates),
	ARRAY_SIZE(rtw_ht_1s_rates)
};

struct rtw_hw_reg {
	u32 addr;
	u32 mask;
};

static const struct rtw_hw_reg rtw8723x_txagc[] = {
	[DESC_RATE1M]	= { .addr = 0xe08, .mask = 0x0000ff00 },
	[DESC_RATE2M]	= { .addr = 0x86c, .mask = 0x0000ff00 },
	[DESC_RATE5_5M]	= { .addr = 0x86c, .mask = 0x00ff0000 },
	[DESC_RATE11M]	= { .addr = 0x86c, .mask = 0xff000000 },
	[DESC_RATE6M]	= { .addr = 0xe00, .mask = 0x000000ff },
	[DESC_RATE9M]	= { .addr = 0xe00, .mask = 0x0000ff00 },
	[DESC_RATE12M]	= { .addr = 0xe00, .mask = 0x00ff0000 },
	[DESC_RATE18M]	= { .addr = 0xe00, .mask = 0xff000000 },
	[DESC_RATE24M]	= { .addr = 0xe04, .mask = 0x000000ff },
	[DESC_RATE36M]	= { .addr = 0xe04, .mask = 0x0000ff00 },
	[DESC_RATE48M]	= { .addr = 0xe04, .mask = 0x00ff0000 },
	[DESC_RATE54M]	= { .addr = 0xe04, .mask = 0xff000000 },
	[DESC_RATEMCS0]	= { .addr = 0xe10, .mask = 0x000000ff },
	[DESC_RATEMCS1]	= { .addr = 0xe10, .mask = 0x0000ff00 },
	[DESC_RATEMCS2]	= { .addr = 0xe10, .mask = 0x00ff0000 },
	[DESC_RATEMCS3]	= { .addr = 0xe10, .mask = 0xff000000 },
	[DESC_RATEMCS4]	= { .addr = 0xe14, .mask = 0x000000ff },
	[DESC_RATEMCS5]	= { .addr = 0xe14, .mask = 0x0000ff00 },
	[DESC_RATEMCS6]	= { .addr = 0xe14, .mask = 0x00ff0000 },
	[DESC_RATEMCS7]	= { .addr = 0xe14, .mask = 0xff000000 },
};

static u8 bcd2bin(u8 val)
{
	return (val & 0x0f) + (val >> 4) * 10;
}

/* TX power by rate from the bb_pg table, in the table's (BCD) units. */
static s8 rtw_phy_pwr_by_rate(u8 rate)
{
	const struct rtw_phy_pg_cfg_pair *p = rtw8723d_bb_pg_tbl.data;
	const struct rtw_hw_reg *reg = &rtw8723x_txagc[rate];
	u32 i;

	for (i = 0; i < rtw8723d_bb_pg_tbl.size; i++, p++) {
		if (p->addr == reg->addr && (p->bitmask & reg->mask) == reg->mask)
			return (s8)bcd2bin((u8)((p->data & reg->mask) >> __ffs(reg->mask)));
	}
	return 0;
}

static s8 rtw_phy_pwr_limit(u8 rs, u8 channel)
{
	const struct rtw_txpwr_lmt_cfg_pair *p = rtw8723d_txpwr_lmt_tbl.data;
	s8 limit = RTW_MAX_POWER_INDEX;
	u32 i;

	for (i = 0; i < rtw8723d_txpwr_lmt_tbl.size; i++, p++) {
		if (p->band == 0 && p->bw == RTW_CHANNEL_WIDTH_20 &&
		    p->rs == rs && p->ch == channel && p->txpwr_lmt < limit)
			limit = p->txpwr_lmt;
	}
	return limit;
}

static u8 rtw_get_channel_group(u8 channel, u8 rate)
{
	if (channel <= 2)
		return 0;
	if (channel <= 5)
		return 1;
	if (channel <= 8)
		return 2;
	if (channel <= 11)
		return 3;
	if (channel <= 13)
		return 4;
	return rate <= DESC_RATE11M ? 5 : 4;
}

static u8 rtw_phy_get_tx_power_index(struct rtw_dev *rtwdev, u8 rs, u8 rate,
				     u8 channel)
{
	const struct rtw_2g_txpwr_idx *idx =
		&rtwdev->efuse.txpwr_idx_table[RF_PATH_A].pwr_idx_2g;
	u8 group = rtw_get_channel_group(channel, rate);
	/* the last rate of a section is its reference ("base") */
	s8 base = rtw_phy_pwr_by_rate(rtw_rate_section[rs][rtw_rate_size[rs] - 1]);
	s8 offset = rtw_phy_pwr_by_rate(rate) - base;
	s8 limit = rtw_phy_pwr_limit(rs, channel) - base;
	int tx_power;

	if (rate <= DESC_RATE11M)
		tx_power = idx->cck_base[group];
	else
		tx_power = idx->bw40_base[group];

	if (rate >= DESC_RATE6M && rate <= DESC_RATE54M)
		tx_power += idx->ht_1s_diff.ofdm;
	if (rate >= DESC_RATEMCS0)
		tx_power += idx->ht_1s_diff.bw20;

	tx_power += min(offset, limit);

	if (tx_power > RTW_MAX_POWER_INDEX)
		tx_power = RTW_MAX_POWER_INDEX;
	if (tx_power < 0)
		tx_power = 0;
	return (u8)tx_power;
}

void rtw_phy_set_tx_power_level(struct rtw_dev *rtwdev, u8 channel)
{
	int rs, j;

	for (rs = 0; rs < RS_NUM; rs++) {
		for (j = 0; j < rtw_rate_size[rs]; j++) {
			u8 rate = rtw_rate_section[rs][j];
			const struct rtw_hw_reg *txagc = &rtw8723x_txagc[rate];
			u8 pwr = rtw_phy_get_tx_power_index(rtwdev, rs, rate, channel);

			rtwdev->tx_pwr_tbl[rate] = pwr;
			rtw_write32_mask(rtwdev, txagc->addr, txagc->mask, pwr);
		}
	}
}

/* ------------------------------------------------- antenna / coexistence */
/* The Bluetooth half of the module is held in reset (PG15 low), so the full
 * rtw88 coexistence state machine is not needed: give the shared antenna and
 * the PTA grant to WLAN once (rtw88 "COEX_SET_ANT_WONLY"). */
#define COEX_GNT_SET_SW_LOW	0x1
#define COEX_GNT_SET_SW_HIGH	0x3
#define COEX_SCBD_ACTIVE	BIT(0)
#define COEX_SCBD_ONOFF		BIT(1)

static bool ltecoex_read_reg(struct rtw_dev *rtwdev, u16 offset, u32 *val)
{
	if (!check_hw_ready(rtwdev, REG_LTECOEX_CTRL, LTECOEX_READY, 1))
		return false;

	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0x800F0000 | offset);
	*val = rtw_read32(rtwdev, REG_LTECOEX_READ_DATA);

	return true;
}

static bool ltecoex_reg_write(struct rtw_dev *rtwdev, u16 offset, u32 value)
{
	if (!check_hw_ready(rtwdev, REG_LTECOEX_CTRL, LTECOEX_READY, 1))
		return false;

	rtw_write32(rtwdev, REG_LTECOEX_WRITE_DATA, value);
	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0xC00F0000 | offset);

	return true;
}

static void rtw_coex_write_indirect_reg(struct rtw_dev *rtwdev, u16 addr,
					u32 mask, u32 val)
{
	u32 shift = __ffs(mask);
	u32 tmp = 0;

	if (!ltecoex_read_reg(rtwdev, addr, &tmp))
		rtw_err(rtwdev, "failed to read indirect register\n");

	tmp = (tmp & (~mask)) | ((val << shift) & mask);

	if (!ltecoex_reg_write(rtwdev, addr, tmp))
		rtw_err(rtwdev, "failed to write indirect register\n");
}

void rtw_coex_wifi_only(struct rtw_dev *rtwdev)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	bool aux = efuse->bt_setting & BIT(6);

	/* rtw_coex_power_on_setting: enable BB, so 0x948 can be written */
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN,
		       BIT_FEN_BB_GLB_RST | BIT_FEN_BB_RSTB);

	/* rtw8723d_coex_cfg_rfe_type: antenna at main or aux */
	if (efuse->share_ant) {
		if (aux)
			rtw_write16(rtwdev, REG_BB_SEL_BTG, 0x80);
		else
			rtw_write16(rtwdev, REG_BB_SEL_BTG, 0x200);
	} else {
		if (aux)
			rtw_write16(rtwdev, REG_BB_SEL_BTG, 0x280);
		else
			rtw_write16(rtwdev, REG_BB_SEL_BTG, 0x0);
	}

	/* disable LTE coex in wifi side */
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, BIT_LTE_COEX_EN, 0x0);
	rtw_coex_write_indirect_reg(rtwdev, LTE_WL_TRX_CTRL, MASKLWORD, 0xffff);
	rtw_coex_write_indirect_reg(rtwdev, LTE_BT_TRX_CTRL, MASKLWORD, 0xffff);

	/* red x issue */
	rtw_write8(rtwdev, 0xff1a, 0x0);

	/* rtw8723d_coex_cfg_gnt_debug */
	rtw_write8_mask(rtwdev, REG_LEDCFG2, BIT(6), 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 3, BIT(0), 0);
	rtw_write8_mask(rtwdev, REG_GPIO_INTM + 2, BIT(4), 0);
	rtw_write8_mask(rtwdev, REG_GPIO_MUXCFG + 2, BIT(1), 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 3, BIT(1), 0);
	rtw_write8_mask(rtwdev, REG_PAD_CTRL1 + 2, BIT(7), 0);
	rtw_write8_mask(rtwdev, REG_SYS_CLKR + 1, BIT(1), 0);
	rtw_write8_mask(rtwdev, REG_SYS_SDIO_CTRL + 3, BIT(3), 0);

	/* rtw8723x_coex_cfg_init */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, 0x1);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_PO_BT_PTA_PINS);
	rtw_write8_set(rtwdev, REG_QUEUE_CTRL, BIT_PTA_WL_TX_EN);

	/* COEX_SET_ANT_WONLY: GNT_BT software low, GNT_WL software high */
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0xc000, COEX_GNT_SET_SW_LOW);
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0x0c00, COEX_GNT_SET_SW_LOW);
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0x3000, COEX_GNT_SET_SW_HIGH);
	rtw_coex_write_indirect_reg(rtwdev, LTE_COEX_CTRL, 0x0300, COEX_GNT_SET_SW_HIGH);

	/* path control owner = WLAN */
	rtw_write8_set(rtwdev, REG_SYS_SDIO_CTRL + 3, BIT_LTE_MUX_CTRL_PATH >> 24);
	rtw_write8_set(rtwdev, REG_BTG_SEL, BIT_MASK_BTG_WL);

	/* scoreboard: WLAN active and on */
	rtw_write16(rtwdev, REG_WIFI_BT_INFO,
		    0x2 | COEX_SCBD_ACTIVE | COEX_SCBD_ONOFF | BIT_BT_INT_EN);

	/* PTA table "all WLAN" */
	rtw_write32(rtwdev, REG_BT_COEX_TABLE0, 0x55555555);
	rtw_write32(rtwdev, REG_BT_COEX_TABLE1, 0x55555555);
}
