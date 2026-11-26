/* rtw_iqk.c - RTL8723D IQ calibration (TX/RX I/Q imbalance of both antenna
 * paths S1 and S0), taken from Linux rtw88 rtw8723d.c / rtw8723x.c with only
 * the driver-state bookkeeping changed.
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation; Copyright 2024 Fiona Klute
 */
#include "rtw_8723x.h"

const u32 rtw8723x_iqk_adda_regs[RTW8723X_IQK_ADDA_REG_NUM] = {
	0x85c, 0xe6c, 0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84,
	0xe88, 0xe8c, 0xed0, 0xed4, 0xed8, 0xedc, 0xee0, 0xeec
};
const u32 rtw8723x_iqk_mac8_regs[RTW8723X_IQK_MAC8_REG_NUM] = {0x522, 0x550, 0x551};
const u32 rtw8723x_iqk_mac32_regs[RTW8723X_IQK_MAC32_REG_NUM] = {0x40};
const u32 rtw8723x_iqk_bb_regs[RTW8723X_IQK_BB_REG_NUM] = {
	0xc04, 0xc08, 0x874, 0xb68, 0xb6c, 0x870, 0x860, 0x864, 0xa04
};

static void rtw8723x_iqk_do_backup(struct rtw_dev *rtwdev,
				struct rtw8723x_iqk_backup_regs *backup)
{
	int i;

	for (i = 0; i < RTW8723X_IQK_ADDA_REG_NUM; i++)
		backup->adda[i] = rtw_read32(rtwdev,
					     rtw8723x_iqk_adda_regs[i]);

	for (i = 0; i < RTW8723X_IQK_MAC8_REG_NUM; i++)
		backup->mac8[i] = rtw_read8(rtwdev,
					    rtw8723x_iqk_mac8_regs[i]);
	for (i = 0; i < RTW8723X_IQK_MAC32_REG_NUM; i++)
		backup->mac32[i] = rtw_read32(rtwdev,
					      rtw8723x_iqk_mac32_regs[i]);

	for (i = 0; i < RTW8723X_IQK_BB_REG_NUM; i++)
		backup->bb[i] = rtw_read32(rtwdev,
					   rtw8723x_iqk_bb_regs[i]);

	backup->igia = rtw_read32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0);
	backup->igib = rtw_read32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0);

	backup->bb_sel_btg = rtw_read32(rtwdev, REG_BB_SEL_BTG);
}

static void rtw8723x_iqk_do_restore(struct rtw_dev *rtwdev,
				 const struct rtw8723x_iqk_backup_regs *backup)
{
	int i;

	for (i = 0; i < RTW8723X_IQK_ADDA_REG_NUM; i++)
		rtw_write32(rtwdev, rtw8723x_iqk_adda_regs[i],
			    backup->adda[i]);

	for (i = 0; i < RTW8723X_IQK_MAC8_REG_NUM; i++)
		rtw_write8(rtwdev, rtw8723x_iqk_mac8_regs[i],
			   backup->mac8[i]);
	for (i = 0; i < RTW8723X_IQK_MAC32_REG_NUM; i++)
		rtw_write32(rtwdev, rtw8723x_iqk_mac32_regs[i],
			    backup->mac32[i]);

	for (i = 0; i < RTW8723X_IQK_BB_REG_NUM; i++)
		rtw_write32(rtwdev, rtw8723x_iqk_bb_regs[i],
			    backup->bb[i]);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, backup->igia);

	rtw_write32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0, backup->igib);

	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x01008c00);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x01008c00);
}

static bool rtw8723x_iqk_similarity_cmp(struct rtw_dev *rtwdev,
				   s32 result[][IQK_NR],
				   u8 c1, u8 c2)
{
	u32 i, j, diff;
	u32 bitmap = 0;
	u8 candidate[PATH_NR] = {IQK_ROUND_INVALID, IQK_ROUND_INVALID};

	(void)rtwdev;
	bool ret = true;

	s32 tmp1, tmp2;

	for (i = 0; i < IQK_NR; i++) {
		tmp1 = iqkxy_to_s32(result[c1][i]);
		tmp2 = iqkxy_to_s32(result[c2][i]);

		diff = abs(tmp1 - tmp2);

		if (diff <= MAX_TOLERANCE)
			continue;

		if ((i == IQK_S1_RX_X || i == IQK_S0_RX_X) && !bitmap) {
			if (result[c1][i] + result[c1][i + 1] == 0)
				candidate[i / IQK_SX_NR] = c2;
			else if (result[c2][i] + result[c2][i + 1] == 0)
				candidate[i / IQK_SX_NR] = c1;
			else
				bitmap |= BIT(i);
		} else {
			bitmap |= BIT(i);
		}
	}

	if (bitmap != 0)
		goto check_sim;

	for (i = 0; i < PATH_NR; i++) {
		if (candidate[i] == IQK_ROUND_INVALID)
			continue;

		for (j = i * IQK_SX_NR; j < i * IQK_SX_NR + 2; j++)
			result[IQK_ROUND_HYBRID][j] = result[candidate[i]][j];
		ret = false;
	}

	return ret;

check_sim:
	for (i = 0; i < IQK_NR; i++) {
		j = i & ~1;	/* 2 bits are a pair for IQ[X, Y] */
		if (bitmap & GENMASK(j + 1, j))
			continue;

		result[IQK_ROUND_HYBRID][i] = result[c1][i];
	}

	return false;
}

struct rtw_8723d_iqk_cfg {
	const char *name;
	u32 val_bb_sel_btg;
	u32 reg_lutwe;
	u32 val_txiqk_pi;
	u32 reg_padlut;
	u32 reg_gaintx;
	u32 reg_bspad;
	u32 val_wlint;
	u32 val_wlsel;
	u32 val_iqkpts;
};

static const struct rtw_8723d_iqk_cfg iqk_tx_cfg[PATH_NR] = {
	[PATH_S1] = {
		.name = "S1",
		.val_bb_sel_btg = 0x99000000,
		.reg_lutwe = RF_LUTWE,
		.val_txiqk_pi = 0x8214019f,
		.reg_padlut = RF_LUTDBG,
		.reg_gaintx = RF_GAINTX,
		.reg_bspad = RF_BSPAD,
		.val_wlint = 0xe0d,
		.val_wlsel = 0x60d,
		.val_iqkpts = 0xfa000000,
	},
	[PATH_S0] = {
		.name = "S0",
		.val_bb_sel_btg = 0x99000280,
		.reg_lutwe = RF_LUTWE2,
		.val_txiqk_pi = 0x8214018a,
		.reg_padlut = RF_TXADBG,
		.reg_gaintx = RF_TRXIQ,
		.reg_bspad = RF_TXATANK,
		.val_wlint = 0xe6d,
		.val_wlsel = 0x66d,
		.val_iqkpts = 0xf9000000,
	},
};

static u8 rtw8723d_iqk_check_tx_failed(struct rtw_dev *rtwdev,
				       const struct rtw_8723d_iqk_cfg *iqk_cfg)
{
	s32 tx_x, tx_y;
	u32 tx_fail;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xeac = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_RY));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xe94 = 0x%x, 0xe9c = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_TX),
		rtw_read32(rtwdev, REG_IQK_RES_TY));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xe90(before IQK)= 0x%x, 0xe98(after IQK) = 0x%x\n",
		rtw_read32(rtwdev, 0xe90),
		rtw_read32(rtwdev, 0xe98));

	tx_fail = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_IQK_TX_FAIL);
	tx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
	tx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);

	if (!tx_fail && tx_x != IQK_TX_X_ERR && tx_y != IQK_TX_Y_ERR)
		return IQK_TX_OK;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] %s TXIQK is failed\n",
		iqk_cfg->name);

	return 0;
}

static u8 rtw8723d_iqk_check_rx_failed(struct rtw_dev *rtwdev,
				       const struct rtw_8723d_iqk_cfg *iqk_cfg)
{
	s32 rx_x, rx_y;
	u32 rx_fail;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xea4 = 0x%x, 0xeac = 0x%x\n",
		rtw_read32(rtwdev, REG_IQK_RES_RX),
		rtw_read32(rtwdev, REG_IQK_RES_RY));

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] 0xea0(before IQK)= 0x%x, 0xea8(after IQK) = 0x%x\n",
		rtw_read32(rtwdev, 0xea0),
		rtw_read32(rtwdev, 0xea8));

	rx_fail = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_IQK_RX_FAIL);
	rx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
	rx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
	rx_y = abs(iqkxy_to_s32(rx_y));

	if (!rx_fail && rx_x < IQK_RX_X_UPPER && rx_x > IQK_RX_X_LOWER &&
	    rx_y < IQK_RX_Y_LMT)
		return IQK_RX_OK;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] %s RXIQK STEP2 is failed\n",
		iqk_cfg->name);

	return 0;
}

#define IQK_LTE_WRITE_VAL_8723D 0x0000ff00

static void rtw8723d_iqk_one_shot(struct rtw_dev *rtwdev, bool tx,
				  const struct rtw_8723d_iqk_cfg *iqk_cfg)
{
	u32 pts = (tx ? iqk_cfg->val_iqkpts : 0xf9000000);

	/* enter IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
	rtw8723x_iqk_config_lte_path_gnt(rtwdev, IQK_LTE_WRITE_VAL_8723D);

	rtw_write32(rtwdev, REG_LTECOEX_CTRL, 0x800f0054);
	mdelay(1);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] GNT_BT @%s %sIQK1 = 0x%x\n",
		iqk_cfg->name, tx ? "TX" : "RX",
		rtw_read32(rtwdev, REG_LTECOEX_READ_DATA));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x948 @%s %sIQK1 = 0x%x\n",
		iqk_cfg->name, tx ? "TX" : "RX",
		rtw_read32(rtwdev, REG_BB_SEL_BTG));

	/* One shot, LOK & IQK */
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, pts);
	rtw_write32(rtwdev, REG_IQK_AGC_PTS_11N, 0xf8000000);

	if (!check_hw_ready(rtwdev, REG_IQK_RES_RY, BIT_IQK_DONE, 1))
		rtw_warn(rtwdev, "%s %s IQK isn't done\n", iqk_cfg->name,
			 tx ? "TX" : "RX");
}

static void rtw8723d_iqk_txrx_path_post(struct rtw_dev *rtwdev,
					const struct rtw_8723d_iqk_cfg *iqk_cfg,
					const struct rtw8723x_iqk_backup_regs *backup)
{
	rtw8723x_iqk_restore_lte_path_gnt(rtwdev, backup);
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup->bb_sel_btg);

	/* leave IQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_padlut, 0x800, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, BIT(0), 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLSEL, BIT(0), 0x0);
}

static u8 rtw8723d_iqk_tx_path(struct rtw_dev *rtwdev,
			       const struct rtw_8723d_iqk_cfg *iqk_cfg,
			       const struct rtw8723x_iqk_backup_regs *backup)
{
	u8 status;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path %s TXIQK!!\n", iqk_cfg->name);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @%s TXIQK = 0x%x\n",
		iqk_cfg->name,
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));

	rtw_write32(rtwdev, REG_BB_SEL_BTG, iqk_cfg->val_bb_sel_btg);
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x80000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00004);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x0005d);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0xBFFE0);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x00000);

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x08008c0c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, iqk_cfg->val_txiqk_pi);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28160200);
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x00462911);

	/* PA, PAD setting */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_padlut, 0x800, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_gaintx, 0x600, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_gaintx, 0x1E0, 0x3);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_RXIQGEN, 0x1F, 0xf);

	/* LOK setting for 8723D */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, 0x10, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_bspad, 0x1, 0x1);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, iqk_cfg->val_wlint);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK, iqk_cfg->val_wlsel);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x1 @%s TXIQK = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x2 @%s TXIQK = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK));

	rtw8723d_iqk_one_shot(rtwdev, true, iqk_cfg);
	status = rtw8723d_iqk_check_tx_failed(rtwdev, iqk_cfg);

	rtw8723d_iqk_txrx_path_post(rtwdev, iqk_cfg, backup);

	return status;
}

static u8 rtw8723d_iqk_rx_path(struct rtw_dev *rtwdev,
			       const struct rtw_8723d_iqk_cfg *iqk_cfg,
			       const struct rtw8723x_iqk_backup_regs *backup)
{
	u32 tx_x, tx_y;
	u8 status;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path %s RXIQK Step1!!\n",
		iqk_cfg->name);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @%s RXIQK1 = 0x%x\n",
		iqk_cfg->name,
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));
	rtw_write32(rtwdev, REG_BB_SEL_BTG, iqk_cfg->val_bb_sel_btg);

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);

	/* IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	/* path IQK setting */
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82160000);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28160000);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a911);

	/* RXIQK mode */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x80000);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00006);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x0005f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0xa7ffb);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x00000);

	/* PA/PAD=0 */
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_padlut, 0x800, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_gaintx, 0x600, 0x0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK, iqk_cfg->val_wlint);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK, iqk_cfg->val_wlsel);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x1@ path %s RXIQK1 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x2@ path %s RXIQK1 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK));

	rtw8723d_iqk_one_shot(rtwdev, false, iqk_cfg);
	status = rtw8723d_iqk_check_tx_failed(rtwdev, iqk_cfg);

	if (!status)
		goto restore;

	/* second round */
	tx_x = rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
	tx_y = rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);

	rtw_write32(rtwdev, REG_TXIQK_11N, BIT_SET_TXIQK_11N(tx_x, tx_y));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0xe40 = 0x%x u4tmp = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQK_11N),
		BIT_SET_TXIQK_11N(tx_x, tx_y));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path %s RXIQK STEP2!!\n",
		iqk_cfg->name);
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] 0x67 @%s RXIQK2 = 0x%x\n",
		iqk_cfg->name,
		rtw_read32_mask(rtwdev, REG_PAD_CTRL1, MASKBYTE3));

	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);
	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x38008c1c);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x18008c1c);
	rtw_write32(rtwdev, REG_TX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_RX_IQK_TONE_B, 0x38008c1c);
	rtw_write32(rtwdev, REG_TXIQK_PI_A_11N, 0x82170000);
	rtw_write32(rtwdev, REG_RXIQK_PI_A_11N, 0x28171400);

	/* LOK setting */
	rtw_write32(rtwdev, REG_IQK_AGC_RSP_11N, 0x0046a8d1);

	/* RXIQK mode */
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, 0x80000, 0x1);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x00007);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD1, RFREG_MASK, 0x0005f);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0xb3fdb);
	rtw_write_rf(rtwdev, RF_PATH_A, iqk_cfg->reg_lutwe, RFREG_MASK, 0x00000);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x1 @%s RXIQK2 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLINT, RFREG_MASK));
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] RF0x2 @%s RXIQK2 = 0x%x\n",
		iqk_cfg->name,
		rtw_read_rf(rtwdev, RF_PATH_A, RF_WLSEL, RFREG_MASK));

	rtw8723d_iqk_one_shot(rtwdev, false, iqk_cfg);
	status |= rtw8723d_iqk_check_rx_failed(rtwdev, iqk_cfg);

restore:
	rtw8723d_iqk_txrx_path_post(rtwdev, iqk_cfg, backup);

	return status;
}

static void rtw8723d_iqk_fill_s1_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
	s32 oldval_1;
	s32 x, y;
	s32 tx1_a, tx1_a_ext;
	s32 tx1_c, tx1_c_ext;

	if (result[IQK_S1_TX_X] == 0)
		return;

	oldval_1 = rtw_read32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
				   BIT_MASK_TXIQ_ELM_D);

	x = iqkxy_to_s32(result[IQK_S1_TX_X]);
	tx1_a = iqk_mult(x, oldval_1, &tx1_a_ext);
	rtw_write32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_A, tx1_a);
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD,
			 BIT_MASK_OFDM0_EXT_A, tx1_a_ext);

	y = iqkxy_to_s32(result[IQK_S1_TX_Y]);
	tx1_c = iqk_mult(y, oldval_1, &tx1_c_ext);
	rtw_write32_mask(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N, MASKH4BITS,
			 BIT_SET_TXIQ_ELM_C1(tx1_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE,
			 BIT_MASK_TXIQ_ELM_C, BIT_SET_TXIQ_ELM_C2(tx1_c));
	rtw_write32_mask(rtwdev, REG_OFDM_0_ECCA_THRESHOLD,
			 BIT_MASK_OFDM0_EXT_C, tx1_c_ext);

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] X = 0x%x, TX1_A = 0x%x, oldval_1 0x%x\n",
		x, tx1_a, oldval_1);
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] Y = 0x%x, TX1_C = 0x%x\n", y, tx1_c);

	if (result[IQK_S1_RX_X] == 0)
		return;

	rtw_write32_mask(rtwdev, REG_A_RXIQI, BIT_MASK_RXIQ_S1_X,
			 result[IQK_S1_RX_X]);
	rtw_write32_mask(rtwdev, REG_A_RXIQI, BIT_MASK_RXIQ_S1_Y1,
			 BIT_SET_RXIQ_S1_Y1(result[IQK_S1_RX_Y]));
	rtw_write32_mask(rtwdev, REG_RXIQK_MATRIX_LSB_11N, BIT_MASK_RXIQ_S1_Y2,
			 BIT_SET_RXIQ_S1_Y2(result[IQK_S1_RX_Y]));
}

static void rtw8723d_iqk_fill_s0_matrix(struct rtw_dev *rtwdev, const s32 result[])
{
	s32 oldval_0;
	s32 x, y;
	s32 tx0_a, tx0_a_ext;
	s32 tx0_c, tx0_c_ext;

	if (result[IQK_S0_TX_X] == 0)
		return;

	oldval_0 = rtw_read32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_D_S0);

	x = iqkxy_to_s32(result[IQK_S0_TX_X]);
	tx0_a = iqk_mult(x, oldval_0, &tx0_a_ext);

	rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_S0, tx0_a);
	rtw_write32_mask(rtwdev, REG_TXIQ_AB_S0, BIT_MASK_TXIQ_A_EXT_S0, tx0_a_ext);

	y = iqkxy_to_s32(result[IQK_S0_TX_Y]);
	tx0_c = iqk_mult(y, oldval_0, &tx0_c_ext);

	rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_S0, tx0_c);
	rtw_write32_mask(rtwdev, REG_TXIQ_CD_S0, BIT_MASK_TXIQ_C_EXT_S0, tx0_c_ext);

	if (result[IQK_S0_RX_X] == 0)
		return;

	rtw_write32_mask(rtwdev, REG_RXIQ_AB_S0, BIT_MASK_RXIQ_X_S0,
			 result[IQK_S0_RX_X]);
	rtw_write32_mask(rtwdev, REG_RXIQ_AB_S0, BIT_MASK_RXIQ_Y_S0,
			 result[IQK_S0_RX_Y]);
}

static void rtw8723d_iqk_config_mac(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_TXPAUSE, 0xff);
}

static void rtw8723d_iqk_rf_standby(struct rtw_dev *rtwdev, enum rtw_rf_path path)
{
	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path-%s standby mode!\n",
		path == RF_PATH_A ? "S1" : "S0");

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);
	rtw_write_rf(rtwdev, path, RF_MODE, RFREG_MASK, 0x10000);
	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
}

#define ADDA_ON_VAL_8723D 0x03c00016

static void rtw8723d_iqk_precfg_path(struct rtw_dev *rtwdev, enum rtw8723x_path path)
{
	if (path == PATH_S0) {
		rtw8723d_iqk_rf_standby(rtwdev, RF_PATH_A);
		rtw8723x_iqk_path_adda_on(rtwdev, ADDA_ON_VAL_8723D);
	}

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, EN_IQK);
	rtw_write32(rtwdev, REG_TXIQK_11N, 0x01007c00);
	rtw_write32(rtwdev, REG_RXIQK_11N, 0x01004800);

	if (path == PATH_S1) {
		rtw8723d_iqk_rf_standby(rtwdev, RF_PATH_B);
		rtw8723x_iqk_path_adda_on(rtwdev, ADDA_ON_VAL_8723D);
	}
}

static void rtw8723d_iqk_one_round(struct rtw_dev *rtwdev, s32 result[][IQK_NR], u8 t,
			    const struct rtw8723x_iqk_backup_regs *backup)
{
	u32 i;
	u8 s1_ok, s0_ok;

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] IQ Calibration for 1T1R_S0/S1 for %d times\n", t);

	rtw8723x_iqk_path_adda_on(rtwdev, ADDA_ON_VAL_8723D);
	rtw8723d_iqk_config_mac(rtwdev);
	rtw_write32_mask(rtwdev, REG_CCK_ANT_SEL_11N, 0x0f000000, 0xf);
	rtw_write32(rtwdev, REG_BB_RX_PATH_11N, 0x03a05611);
	rtw_write32(rtwdev, REG_TRMUX_11N, 0x000800e4);
	rtw_write32(rtwdev, REG_BB_PWR_SAV1_11N, 0x25204200);
	rtw8723d_iqk_precfg_path(rtwdev, PATH_S1);

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s1_ok = rtw8723d_iqk_tx_path(rtwdev, &iqk_tx_cfg[PATH_S1], backup);
		if (s1_ok == IQK_TX_OK) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S1 Tx IQK Success!!\n");
			result[t][IQK_S1_TX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
			result[t][IQK_S1_TX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S1 Tx IQK Fail!!\n");
		result[t][IQK_S1_TX_X] = 0x100;
		result[t][IQK_S1_TX_Y] = 0x0;
	}

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s1_ok = rtw8723d_iqk_rx_path(rtwdev, &iqk_tx_cfg[PATH_S1], backup);
		if (s1_ok == (IQK_TX_OK | IQK_RX_OK)) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S1 Rx IQK Success!!\n");
			result[t][IQK_S1_RX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
			result[t][IQK_S1_RX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S1 Rx IQK Fail!!\n");
		result[t][IQK_S1_RX_X] = 0x100;
		result[t][IQK_S1_RX_Y] = 0x0;
	}

	if (s1_ok == 0x0)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S1 IQK is failed!!\n");

	rtw8723d_iqk_precfg_path(rtwdev, PATH_S0);

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s0_ok = rtw8723d_iqk_tx_path(rtwdev, &iqk_tx_cfg[PATH_S0], backup);
		if (s0_ok == IQK_TX_OK) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S0 Tx IQK Success!!\n");
			result[t][IQK_S0_TX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TX, BIT_MASK_RES_TX);
			result[t][IQK_S0_TX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_TY, BIT_MASK_RES_TY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S0 Tx IQK Fail!!\n");
		result[t][IQK_S0_TX_X] = 0x100;
		result[t][IQK_S0_TX_Y] = 0x0;
	}

	for (i = 0; i < PATH_IQK_RETRY; i++) {
		s0_ok = rtw8723d_iqk_rx_path(rtwdev, &iqk_tx_cfg[PATH_S0], backup);
		if (s0_ok == (IQK_TX_OK | IQK_RX_OK)) {
			rtw_dbg(rtwdev, RTW_DBG_RFK,
				"[IQK] path S0 Rx IQK Success!!\n");

			result[t][IQK_S0_RX_X] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RX, BIT_MASK_RES_RX);
			result[t][IQK_S0_RX_Y] =
			  rtw_read32_mask(rtwdev, REG_IQK_RES_RY, BIT_MASK_RES_RY);
			break;
		}

		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S0 Rx IQK Fail!!\n");
		result[t][IQK_S0_RX_X] = 0x100;
		result[t][IQK_S0_RX_Y] = 0x0;
	}

	if (s0_ok == 0x0)
		rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] path S0 IQK is failed!!\n");

	rtw_write32_mask(rtwdev, REG_FPGA0_IQK_11N, BIT_MASK_IQK_MOD, RST_IQK);
	mdelay(1);

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK] back to BB mode, load original value!\n");
}

void rtw8723d_phy_calibration(struct rtw_dev *rtwdev)
{
	s32 result[IQK_ROUND_SIZE][IQK_NR];
	struct rtw8723x_iqk_backup_regs backup;
	u8 i, j;
	u8 final_candidate = IQK_ROUND_INVALID;
	bool good;

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] Start!!!\n");

	memset(result, 0, sizeof(result));

	rtw8723x_iqk_backup_path_ctrl(rtwdev, &backup);
	rtw8723x_iqk_backup_lte_path_gnt(rtwdev, &backup);
	rtw8723x_iqk_do_backup(rtwdev, &backup);

	for (i = IQK_ROUND_0; i <= IQK_ROUND_2; i++) {
		rtw8723x_iqk_config_path_ctrl(rtwdev);
		rtw8723x_iqk_config_lte_path_gnt(rtwdev, IQK_LTE_WRITE_VAL_8723D);

		rtw8723d_iqk_one_round(rtwdev, result, i, &backup);

		if (i > IQK_ROUND_0)
			rtw8723x_iqk_do_restore(rtwdev, &backup);
		rtw8723x_iqk_restore_lte_path_gnt(rtwdev, &backup);
		rtw8723x_iqk_restore_path_ctrl(rtwdev, &backup);

		for (j = IQK_ROUND_0; j < i; j++) {
			good = rtw8723x_iqk_similarity_cmp(rtwdev, result, j, i);

			if (good) {
				final_candidate = j;
				rtw_dbg(rtwdev, RTW_DBG_RFK,
					"[IQK] cmp %d:%d final_candidate is %x\n",
					j, i, final_candidate);
				goto iqk_done;
			}
		}
	}

	if (final_candidate == IQK_ROUND_INVALID) {
		s32 reg_tmp = 0;

		for (i = 0; i < IQK_NR; i++)
			reg_tmp += result[IQK_ROUND_HYBRID][i];

		if (reg_tmp != 0) {
			final_candidate = IQK_ROUND_HYBRID;
		} else {
			WARN(1, "IQK is failed\n");
			goto out;
		}
	}

iqk_done:
	rtw8723d_iqk_fill_s1_matrix(rtwdev, result[final_candidate]);
	rtw8723d_iqk_fill_s0_matrix(rtwdev, result[final_candidate]);

	rtwdev->iqk_done = true;

out:
	rtw_write32(rtwdev, REG_BB_SEL_BTG, backup.bb_sel_btg);

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] final_candidate is %x\n",
		final_candidate);

	for (i = IQK_ROUND_0; i < IQK_ROUND_SIZE; i++)
		rtw_dbg(rtwdev, RTW_DBG_RFK,
			"[IQK] Result %u: rege94_s1=%x rege9c_s1=%x regea4_s1=%x regeac_s1=%x rege94_s0=%x rege9c_s0=%x regea4_s0=%x regeac_s0=%x %s\n",
			i,
			result[i][0], result[i][1], result[i][2], result[i][3],
			result[i][4], result[i][5], result[i][6], result[i][7],
			final_candidate == i ? "(final candidate)" : "");

	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK]0xc80 = 0x%x 0xc94 = 0x%x 0xc14 = 0x%x 0xca0 = 0x%x\n",
		rtw_read32(rtwdev, REG_OFDM_0_XA_TX_IQ_IMBALANCE),
		rtw_read32(rtwdev, REG_TXIQK_MATRIXA_LSB2_11N),
		rtw_read32(rtwdev, REG_A_RXIQI),
		rtw_read32(rtwdev, REG_RXIQK_MATRIX_LSB_11N));
	rtw_dbg(rtwdev, RTW_DBG_RFK,
		"[IQK]0xcd0 = 0x%x 0xcd4 = 0x%x 0xcd8 = 0x%x\n",
		rtw_read32(rtwdev, REG_TXIQ_AB_S0),
		rtw_read32(rtwdev, REG_TXIQ_CD_S0),
		rtw_read32(rtwdev, REG_RXIQ_AB_S0));

	rtw_dbg(rtwdev, RTW_DBG_RFK, "[IQK] finished\n");
}

