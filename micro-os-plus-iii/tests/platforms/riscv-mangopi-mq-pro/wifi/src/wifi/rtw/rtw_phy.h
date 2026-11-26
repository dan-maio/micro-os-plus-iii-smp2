/* rtw_phy.h - parameter table plumbing (rtw88 phy.h / main.h).
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef MQPRO_RTW_PHY_H
#define MQPRO_RTW_PHY_H

#include "rtw.h"

struct rtw_table {
	const void *data;
	const u32 size;
	void (*do_cfg)(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		       u32 addr, u32 data);
	enum rtw_rf_path rf_path;
};

struct rtw_phy_pg_cfg_pair {
	u32 band;
	u32 rf_path;
	u32 tx_num;
	u32 addr;
	u32 bitmask;
	u32 data;
};

struct rtw_txpwr_lmt_cfg_pair {
	u8 regd;
	u8 band;
	u8 bw;
	u8 rs;
	u8 ch;
	s8 txpwr_lmt;
};

void rtw_phy_cfg_mac(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data);
void rtw_phy_cfg_agc(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data);
void rtw_phy_cfg_bb(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data);
void rtw_phy_cfg_rf(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data);

#define RTW_DECL_TABLE_PHY_COND_CORE(name, cfg, path)	\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.do_cfg = cfg,					\
	.rf_path = path,				\
}

#define RTW_DECL_TABLE_PHY_COND(name, cfg)		\
	RTW_DECL_TABLE_PHY_COND_CORE(name, cfg, 0)

#define RTW_DECL_TABLE_RF_RADIO(name, path)		\
	RTW_DECL_TABLE_PHY_COND_CORE(name, rtw_phy_cfg_rf, RF_PATH_ ## path)

#define RTW_DECL_TABLE_BB_PG(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
}

#define RTW_DECL_TABLE_TXPWR_LMT(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
}

extern const struct rtw_table rtw8723d_mac_tbl;
extern const struct rtw_table rtw8723d_agc_tbl;
extern const struct rtw_table rtw8723d_bb_tbl;
extern const struct rtw_table rtw8723d_bb_pg_tbl;
extern const struct rtw_table rtw8723d_rf_a_tbl;
extern const struct rtw_table rtw8723d_txpwr_lmt_tbl;

#endif
