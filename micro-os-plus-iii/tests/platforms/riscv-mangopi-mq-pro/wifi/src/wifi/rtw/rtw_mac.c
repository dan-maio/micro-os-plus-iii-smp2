/* rtw_mac.c - RTL8723D MAC power sequence, firmware download, MAC init and
 * efuse, ported from Linux rtw88 (mac.c, efuse.c, rtw8723d.c, rtw8723x.c).
 * Only the "legacy" (8051 firmware CPU) + SDIO paths are kept.
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation
 */
#include "rtw.h"

#define RTW_PWR_POLLING_CNT	20000

#define RTW_PWR_CMD_READ	0x00
#define RTW_PWR_CMD_WRITE	0x01
#define RTW_PWR_CMD_POLLING	0x02
#define RTW_PWR_CMD_DELAY	0x03
#define RTW_PWR_CMD_END		0x04

#define RTW_PWR_ADDR_MAC	0x00
#define RTW_PWR_ADDR_USB	0x01
#define RTW_PWR_ADDR_PCIE	0x02
#define RTW_PWR_ADDR_SDIO	0x03

#define RTW_PWR_INTF_SDIO_MSK	BIT(0)
#define RTW_PWR_INTF_USB_MSK	BIT(1)
#define RTW_PWR_INTF_PCI_MSK	BIT(2)
#define RTW_PWR_INTF_ALL_MSK	(BIT(0) | BIT(1) | BIT(2) | BIT(3))

#define RTW_PWR_CUT_TEST_MSK	BIT(0)
#define RTW_PWR_CUT_A_MSK	BIT(1)
#define RTW_PWR_CUT_B_MSK	BIT(2)
#define RTW_PWR_CUT_C_MSK	BIT(3)
#define RTW_PWR_CUT_D_MSK	BIT(4)
#define RTW_PWR_CUT_E_MSK	BIT(5)
#define RTW_PWR_CUT_F_MSK	BIT(6)
#define RTW_PWR_CUT_G_MSK	BIT(7)
#define RTW_PWR_CUT_ALL_MSK	0xFF

#define cut_version_to_mask(cut) (0x1 << ((cut) + 1))

enum rtw_pwr_seq_cmd_delay_unit {
	RTW_PWR_DELAY_US,
	RTW_PWR_DELAY_MS,
};

struct rtw_pwr_seq_cmd {
	u16 offset;
	u8 cut_mask;
	u8 intf_mask;
	u8 base:4;
	u8 cmd:4;
	u8 mask;
	u8 value;
};

/* ---- power sequences, unchanged from rtw8723d.c ---- */
static const struct rtw_pwr_seq_cmd trans_carddis_to_cardemu_8723d[] = {
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(7), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), 0},
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), 0},
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_act_8723d[] = {
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0001,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 1, RTW_PWR_DELAY_MS},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3) | BIT(2)), 0},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), BIT(1)},
	{0x0075,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, (BIT(1) | BIT(0)), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(7), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(0), 0},
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0063,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0062,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0058,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x005A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0068,
	 RTW_PWR_CUT_TEST_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3), BIT(3)},
	{0x0069,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), BIT(6)},
	{0x001f,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0077,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x001f,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x07},
	{0x0077,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x07},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd * const card_enable_flow_8723d[] = {
	trans_carddis_to_cardemu_8723d,
	trans_cardemu_to_act_8723d,
	NULL
};

__attribute__((unused)) static const struct rtw_pwr_seq_cmd trans_act_to_lps_8723d[] = {
	{0x0301,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x0522,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0xFF},
	{0x05F8,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05F9,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FA,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x05FB,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, 0xFF, 0},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_DELAY, 0, RTW_PWR_DELAY_US},
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0100,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x03},
	{0x0101,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0093,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x00},
	{0x0553,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_pre_carddis_8723d[] = {
	{0x0003,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), 0},
	{0x0080,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_cardemu_8723d[] = {
	{0x0002,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x0049,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), 0},
	{0x0006,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(1), BIT(1)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0x0010,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(6), 0},
	{0x0000,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(5), BIT(5)},
	{0x0020,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_cardemu_to_carddis_8723d[] = {
	{0x0007,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x20},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK | RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(2), BIT(2)},
	{0x0005,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_PCI_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) | BIT(4)},
	{0x004A,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_USB_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 1},
	{0x0023,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(4), BIT(4)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x0086,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_SDIO_MSK,
	 RTW_PWR_ADDR_SDIO,
	 RTW_PWR_CMD_POLLING, BIT(1), 0},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd trans_act_to_post_carddis_8723d[] = {
	{0x001D,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), 0},
	{0x001D,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, BIT(0), BIT(0)},
	{0x001C,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 RTW_PWR_ADDR_MAC,
	 RTW_PWR_CMD_WRITE, 0xFF, 0x0E},
	{0xFFFF,
	 RTW_PWR_CUT_ALL_MSK,
	 RTW_PWR_INTF_ALL_MSK,
	 0,
	 RTW_PWR_CMD_END, 0, 0},
};

static const struct rtw_pwr_seq_cmd * const card_disable_flow_8723d[] = {
	trans_act_to_lps_8723d,
	trans_act_to_pre_carddis_8723d,
	trans_act_to_cardemu_8723d,
	trans_cardemu_to_carddis_8723d,
	trans_act_to_post_carddis_8723d,
	NULL
};


/* ------------------------------------------------------------ power on */
static int rtw_mac_pre_system_cfg(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_RSV_CTRL, 0);

	if (rtw_read32(rtwdev, REG_SYS_CFG1) & BIT_LDO)
		rtw_write8(rtwdev, REG_LDO_SWR_CTRL, LDO_SEL);
	else
		rtw_write8(rtwdev, REG_LDO_SWR_CTRL, SPS_SEL);
	return 0;
}

static bool do_pwr_poll_cmd(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target)
{
	u32 cnt;

	target &= mask;
	for (cnt = 0; cnt < RTW_PWR_POLLING_CNT; cnt++) {
		if ((rtw_read8(rtwdev, addr) & mask) == target)
			return true;
		udelay(50);
	}
	return false;
}

static int rtw_pwr_cmd_polling(struct rtw_dev *rtwdev,
			       const struct rtw_pwr_seq_cmd *cmd)
{
	u32 offset;

	if (cmd->base == RTW_PWR_ADDR_SDIO)
		offset = cmd->offset | SDIO_LOCAL_OFFSET;
	else
		offset = cmd->offset;

	if (do_pwr_poll_cmd(rtwdev, offset, cmd->mask, cmd->value))
		return 0;

	rtw_err(rtwdev, "failed to poll offset=0x%x mask=0x%x value=0x%x\n",
		offset, cmd->mask, cmd->value);
	return -EBUSY;
}

static int rtw_sub_pwr_seq_parser(struct rtw_dev *rtwdev, u8 intf_mask,
				  u8 cut_mask,
				  const struct rtw_pwr_seq_cmd *cmd)
{
	const struct rtw_pwr_seq_cmd *cur_cmd;
	u32 offset;
	u8 value;

	for (cur_cmd = cmd; cur_cmd->cmd != RTW_PWR_CMD_END; cur_cmd++) {
		if (!(cur_cmd->intf_mask & intf_mask) ||
		    !(cur_cmd->cut_mask & cut_mask))
			continue;

		switch (cur_cmd->cmd) {
		case RTW_PWR_CMD_WRITE:
			offset = cur_cmd->offset;

			if (cur_cmd->base == RTW_PWR_ADDR_SDIO)
				offset |= SDIO_LOCAL_OFFSET;

			value = rtw_read8(rtwdev, offset);
			value &= ~cur_cmd->mask;
			value |= (cur_cmd->value & cur_cmd->mask);
			rtw_write8(rtwdev, offset, value);
			break;
		case RTW_PWR_CMD_POLLING:
			if (rtw_pwr_cmd_polling(rtwdev, cur_cmd))
				return -EBUSY;
			break;
		case RTW_PWR_CMD_DELAY:
			if (cur_cmd->value == RTW_PWR_DELAY_US)
				udelay(cur_cmd->offset);
			else
				mdelay(cur_cmd->offset);
			break;
		case RTW_PWR_CMD_READ:
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int rtw_pwr_seq_parser(struct rtw_dev *rtwdev,
			      const struct rtw_pwr_seq_cmd * const *cmd_seq)
{
	u8 cut_mask = cut_version_to_mask(rtwdev->cut_version);
	u32 idx = 0;
	int ret;

	for (;;) {
		const struct rtw_pwr_seq_cmd *cmd = cmd_seq[idx];

		if (!cmd)
			break;
		ret = rtw_sub_pwr_seq_parser(rtwdev, RTW_PWR_INTF_SDIO_MSK,
					     cut_mask, cmd);
		if (ret)
			return ret;
		idx++;
	}
	return 0;
}

static int rtw_mac_power_switch(struct rtw_dev *rtwdev, bool pwr_on)
{
	const struct rtw_pwr_seq_cmd * const *pwr_seq;
	bool cur_pwr;
	u32 imr;
	int ret;

	cur_pwr = rtw_read8(rtwdev, REG_CR) != 0xea;
	if (pwr_on == cur_pwr)
		return -EALREADY;

	imr = rtw_read32(rtwdev, REG_SDIO_HIMR);
	rtw_write32(rtwdev, REG_SDIO_HIMR, 0);

	if (!pwr_on)
		rtwdev->powered = false;

	pwr_seq = pwr_on ? card_enable_flow_8723d : card_disable_flow_8723d;
	ret = rtw_pwr_seq_parser(rtwdev, pwr_seq);

	rtw_write32(rtwdev, REG_SDIO_HIMR, imr);

	if (!ret && pwr_on)
		rtwdev->powered = true;
	return ret;
}

static int rtw_mac_init_system_cfg(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_CR, 0xff);
	mdelay(2);
	rtw_write8(rtwdev, REG_HWSEQ_CTRL, 0x7f);
	mdelay(2);

	rtw_write8_set(rtwdev, REG_SYS_CLKR, BIT_WAKEPAD_EN);
	rtw_write16_clr(rtwdev, REG_GPIO_MUXCFG, BIT_EN_SIC);

	rtw_write16(rtwdev, REG_CR, 0x2ff);
	return 0;
}

int rtw_mac_power_on(struct rtw_dev *rtwdev)
{
	int ret;

	rtwdev->chip_version = rtw_read32(rtwdev, REG_SYS_CFG1);
	rtwdev->cut_version = BIT_GET_CHIP_VER(rtwdev->chip_version);

	ret = rtw_mac_pre_system_cfg(rtwdev);
	if (ret)
		goto err;

	ret = rtw_mac_power_switch(rtwdev, true);
	if (ret == -EALREADY) {
		rtw_mac_power_switch(rtwdev, false);

		ret = rtw_mac_pre_system_cfg(rtwdev);
		if (ret)
			goto err;

		ret = rtw_mac_power_switch(rtwdev, true);
		if (ret)
			goto err;
	} else if (ret) {
		goto err;
	}

	ret = rtw_mac_init_system_cfg(rtwdev);
	if (ret)
		goto err;

	return 0;

err:
	rtw_err(rtwdev, "mac power on failed (%d)\n", ret);
	return ret;
}

void rtw_mac_power_off(struct rtw_dev *rtwdev)
{
	rtw_mac_power_switch(rtwdev, false);
	rtwdev->fw_running = false;
}

/* ---------------------------------------------------- firmware download */
#define DLFW_PAGE_SIZE_SHIFT_LEGACY	12
#define DLFW_PAGE_SIZE_LEGACY		0x1000
#define DLFW_BLK_SIZE_SHIFT_LEGACY	2
#define DLFW_BLK_SIZE_LEGACY		4
#define FW_START_ADDR_LEGACY		0x1000

struct rtw_fw_hdr_legacy {
	__le16 signature;
	u8 category;
	u8 function;
	__le16 version;	/* 0x04 */
	u8 subversion1;
	u8 subversion2;
	u8 month;	/* 0x08 */
	u8 day;
	u8 hour;
	u8 minute;
	__le16 size;
	__le16 rsvd2;
	__le32 idx;	/* 0x10 */
	__le32 rsvd3;
	__le32 rsvd4;	/* 0x18 */
	__le32 rsvd5;
} __packed;

static void wlan_cpu_enable(struct rtw_dev *rtwdev, bool enable)
{
	if (enable) {
		/* cpu io interface enable */
		rtw_write8_set(rtwdev, REG_RSV_CTRL + 1, BIT_WLMCU_IOIF);

		/* cpu enable */
		rtw_write8_set(rtwdev, REG_SYS_FUNC_EN + 1, BIT_FEN_CPUEN);
	} else {
		/* cpu io interface disable */
		rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN + 1, BIT_FEN_CPUEN);

		/* cpu disable */
		rtw_write8_clr(rtwdev, REG_RSV_CTRL + 1, BIT_WLMCU_IOIF);
	}
}

static void en_download_firmware_legacy(struct rtw_dev *rtwdev, bool en)
{
	int try;

	if (en) {
		wlan_cpu_enable(rtwdev, false);
		wlan_cpu_enable(rtwdev, true);

		rtw_write8_set(rtwdev, REG_MCUFW_CTRL, BIT_MCUFWDL_EN);

		for (try = 0; try < 10; try++) {
			if (rtw_read8(rtwdev, REG_MCUFW_CTRL) & BIT_MCUFWDL_EN)
				goto fwdl_ready;
			rtw_write8_set(rtwdev, REG_MCUFW_CTRL, BIT_MCUFWDL_EN);
			msleep(20);
		}
		rtw_err(rtwdev, "failed to check fw download ready\n");
fwdl_ready:
		rtw_write32_clr(rtwdev, REG_MCUFW_CTRL, BIT_ROM_DLEN);
	} else {
		rtw_write8_clr(rtwdev, REG_MCUFW_CTRL, BIT_MCUFWDL_EN);
	}
}

static void rtw_write_firmware_page(struct rtw_dev *rtwdev, u32 page,
				    const u8 *data, u32 size)
{
	u32 val32;
	u32 block_nr;
	u32 remain_size;
	u32 write_addr = FW_START_ADDR_LEGACY;
	u32 block;
	u32 word;

	block_nr = size >> DLFW_BLK_SIZE_SHIFT_LEGACY;
	remain_size = size & (DLFW_BLK_SIZE_LEGACY - 1);

	val32 = rtw_read32(rtwdev, REG_MCUFW_CTRL);
	val32 &= ~BIT_ROM_PGE;
	val32 |= (page << BIT_SHIFT_ROM_PGE) & BIT_ROM_PGE;
	rtw_write32(rtwdev, REG_MCUFW_CTRL, val32);

	for (block = 0; block < block_nr; block++) {
		memcpy(&word, data, 4);
		rtw_write32(rtwdev, write_addr, le32_to_cpu(word));

		write_addr += DLFW_BLK_SIZE_LEGACY;
		data += 4;
	}

	if (remain_size) {
		word = 0;
		memcpy(&word, data, remain_size);
		rtw_write32(rtwdev, write_addr, le32_to_cpu(word));
	}
}

static int download_firmware_legacy(struct rtw_dev *rtwdev, const u8 *data,
				    u32 size)
{
	u32 page;
	u32 total_page;
	u32 last_page_size;

	data += sizeof(struct rtw_fw_hdr_legacy);
	size -= sizeof(struct rtw_fw_hdr_legacy);

	total_page = size >> DLFW_PAGE_SIZE_SHIFT_LEGACY;
	last_page_size = size & (DLFW_PAGE_SIZE_LEGACY - 1);

	rtw_write8_set(rtwdev, REG_MCUFW_CTRL, BIT_FWDL_CHK_RPT);

	for (page = 0; page < total_page; page++) {
		rtw_write_firmware_page(rtwdev, page, data, DLFW_PAGE_SIZE_LEGACY);
		data += DLFW_PAGE_SIZE_LEGACY;
	}
	if (last_page_size)
		rtw_write_firmware_page(rtwdev, page, data, last_page_size);

	if (!check_hw_ready(rtwdev, REG_MCUFW_CTRL, BIT_FWDL_CHK_RPT, 1)) {
		rtw_err(rtwdev, "failed to check download firmware report\n");
		return -EINVAL;
	}

	return 0;
}

static int download_firmware_validate_legacy(struct rtw_dev *rtwdev)
{
	u32 val32;
	int try;

	val32 = rtw_read32(rtwdev, REG_MCUFW_CTRL);
	val32 |= BIT_MCUFWDL_RDY;
	val32 &= ~BIT_WINTINI_RDY;
	rtw_write32(rtwdev, REG_MCUFW_CTRL, val32);

	wlan_cpu_enable(rtwdev, false);
	wlan_cpu_enable(rtwdev, true);

	for (try = 0; try < 10; try++) {
		val32 = rtw_read32(rtwdev, REG_MCUFW_CTRL);
		if ((val32 & FW_READY_LEGACY) == FW_READY_LEGACY)
			return 0;
		msleep(20);
	}

	rtw_err(rtwdev, "failed to validate firmware (MCUFW_CTRL=0x%08x)\n", val32);
	return -EINVAL;
}

int rtw_download_firmware(struct rtw_dev *rtwdev, const u8 *data, u32 size)
{
	const struct rtw_fw_hdr_legacy *hdr = (const void *)data;
	int ret;

	if (size <= sizeof(*hdr))
		return -EINVAL;
	rtwdev->fw_version = le16_to_cpu(hdr->version);
	rtwdev->fw_sub_version = hdr->subversion1;

	en_download_firmware_legacy(rtwdev, true);
	ret = download_firmware_legacy(rtwdev, data, size);
	en_download_firmware_legacy(rtwdev, false);
	if (ret)
		return ret;

	ret = download_firmware_validate_legacy(rtwdev);
	if (ret)
		return ret;

	rtwdev->h2c_box = 0;
	rtwdev->fw_running = true;
	return 0;
}

/* ------------------------------------------------------------- MAC init */
/* SDIO row of rtw8723d rqpn_table / page_table */
#define RTW_DMA_MAPPING_EXTRA	0
#define RTW_DMA_MAPPING_LOW	1
#define RTW_DMA_MAPPING_NORMAL	2
#define RTW_DMA_MAPPING_HIGH	3

#define PG_HQ_NUM	12
#define PG_NQ_NUM	2
#define PG_LQ_NUM	2
#define PG_EXQ_NUM	0
#define PG_GAPQ_NUM	1

static int txdma_queue_mapping(struct rtw_dev *rtwdev)
{
	u16 txdma_pq_map = 0;

	/* vo, vi = normal; be, bk = low; mg = extra; hi = high */
	txdma_pq_map |= BIT_TXDMA_HIQ_MAP(RTW_DMA_MAPPING_HIGH);
	txdma_pq_map |= BIT_TXDMA_MGQ_MAP(RTW_DMA_MAPPING_EXTRA);
	txdma_pq_map |= BIT_TXDMA_BKQ_MAP(RTW_DMA_MAPPING_LOW);
	txdma_pq_map |= BIT_TXDMA_BEQ_MAP(RTW_DMA_MAPPING_LOW);
	txdma_pq_map |= BIT_TXDMA_VIQ_MAP(RTW_DMA_MAPPING_NORMAL);
	txdma_pq_map |= BIT_TXDMA_VOQ_MAP(RTW_DMA_MAPPING_NORMAL);
	rtw_write16(rtwdev, REG_TXDMA_PQ_MAP, txdma_pq_map);

	rtw_write8(rtwdev, REG_CR, 0);
	rtw_write8(rtwdev, REG_CR, MAC_TRX_ENABLE);

	rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);
	rtw_write32(rtwdev, REG_SDIO_TX_CTRL, 0);
	return 0;
}

static int priority_queue_cfg(struct rtw_dev *rtwdev)
{
	u16 txff_pg_num = RTW_TXFF_SIZE / TX_PAGE_SIZE;
	u16 pubq_num;
	u32 val32;

	rtwdev->acq_pg_num = txff_pg_num - RTW_RSVD_DRV_PG_NUM;
	rtwdev->rsvd_boundary = txff_pg_num - RTW_RSVD_DRV_PG_NUM;

	pubq_num = rtwdev->acq_pg_num - PG_HQ_NUM - PG_LQ_NUM - PG_NQ_NUM -
		   PG_EXQ_NUM - PG_GAPQ_NUM;

	val32 = BIT_RQPN_NE(PG_NQ_NUM, PG_EXQ_NUM);
	rtw_write32(rtwdev, REG_RQPN_NPQ, val32);
	val32 = BIT_RQPN_HLP(PG_HQ_NUM, PG_LQ_NUM, pubq_num);
	rtw_write32(rtwdev, REG_RQPN, val32);

	rtw_write8(rtwdev, REG_TRXFF_BNDY, rtwdev->rsvd_boundary);
	rtw_write16(rtwdev, REG_TRXFF_BNDY + 2, RTW_RXFF_SIZE - REPORT_BUF - 1);
	rtw_write8(rtwdev, REG_DWBCN0_CTRL + 1, rtwdev->rsvd_boundary);
	rtw_write8(rtwdev, REG_BCNQ_BDNY, rtwdev->rsvd_boundary);
	rtw_write8(rtwdev, REG_MGQ_BDNY, rtwdev->rsvd_boundary);
	rtw_write8(rtwdev, REG_WMAC_LBK_BF_HD, rtwdev->rsvd_boundary);

	rtw_write32_set(rtwdev, REG_AUTO_LLT, BIT_AUTO_INIT_LLT);

	if (!check_hw_ready(rtwdev, REG_AUTO_LLT, BIT_AUTO_INIT_LLT, 0))
		return -EBUSY;

	return 0;
}

#define BIT_CFENDFORM		BIT(9)
#define BIT_WMAC_TCR_ERR0	BIT(12)
#define BIT_WMAC_TCR_ERR1	BIT(13)
#define BIT_TCR_CFG		(BIT_CFENDFORM | BIT_WMAC_TCR_ERR0 |	       \
				 BIT_WMAC_TCR_ERR1)
#define WLAN_RX_FILTER0		0xFFFF
#define WLAN_RX_FILTER1		0x400
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0x700060CE

static int rtw8723x_mac_init(struct rtw_dev *rtwdev)
{
	rtw_write32(rtwdev, REG_TCR, BIT_TCR_CFG);

	rtw_write16(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP1, WLAN_RX_FILTER1);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);

	rtw_write32(rtwdev, REG_INT_MIG, 0);
	rtw_write32(rtwdev, REG_MCUTST_1, 0x0);

	rtw_write8(rtwdev, REG_MISC_CTRL, BIT_DIS_SECOND_CCA);
	rtw_write8(rtwdev, REG_2ND_CCA_CTRL, 0);

	return 0;
}

static int rtw_drv_info_cfg(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_RX_DRVINFO_SZ, PHY_STATUS_SIZE);
	rtw_write32_set(rtwdev, REG_RCR, BIT_APP_PHYSTS);
	rtw_write32_clr(rtwdev, REG_WMAC_OPTION_FUNCTION + 4, BIT(8) | BIT(9));

	return 0;
}

static void rtw_sdio_interface_cfg(struct rtw_dev *rtwdev)
{
	u32 val;

	rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);

	val = rtw_read32(rtwdev, REG_SDIO_TX_CTRL);
	val &= 0xfff8;
	rtw_write32(rtwdev, REG_SDIO_TX_CTRL, val);
}

int rtw_mac_init(struct rtw_dev *rtwdev)
{
	int ret;

	ret = txdma_queue_mapping(rtwdev);
	if (ret)
		return ret;

	ret = priority_queue_cfg(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "LLT init failed\n");
		return ret;
	}

	ret = rtw8723x_mac_init(rtwdev);
	if (ret)
		return ret;

	ret = rtw_drv_info_cfg(rtwdev);
	if (ret)
		return ret;

	rtw_sdio_interface_cfg(rtwdev);

	return 0;
}

void rtw_set_channel_mac(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	u32 value32;

	(void)channel;
	rtw_write8(rtwdev, REG_DATA_SC, 0);

	value32 = rtw_read32(rtwdev, REG_WMAC_TRXPTCL_CTL);
	value32 &= ~BIT_RFMOD;
	if (bw == RTW_CHANNEL_WIDTH_40)
		value32 |= BIT_RFMOD_40M;
	rtw_write32(rtwdev, REG_WMAC_TRXPTCL_CTL, value32);
}

/* ---------------------------------------------------------------- efuse */
#define RTW_EFUSE_BANK_WIFI		0x0

/* logical map layout, from rtw8723x.h */
struct rtw8723xs_efuse {
	u8 res4[0x4a];			/* 0xd0 */
	u8 mac_addr[ETH_ALEN];		/* 0x11a */
} __packed;

struct rtw8723x_efuse {
	__le16 rtl_id;
	u8 rsvd[2];
	u8 afe;
	u8 rsvd1[11];

	/* power index for four RF paths */
	struct rtw_txpwr_idx txpwr_idx_table[4];

	u8 channel_plan;		/* 0xb8 */
	u8 xtal_k;
	u8 thermal_meter;
	u8 iqk_lck;
	u8 pa_type;			/* 0xbc */
	u8 lna_type_2g[2];		/* 0xbd */
	u8 lna_type_5g[2];
	u8 rf_board_option;
	u8 rf_feature_option;
	u8 rf_bt_setting;
	u8 eeprom_version;
	u8 eeprom_customer_id;
	u8 tx_bb_swing_setting_2g;
	u8 res_c7;
	u8 tx_pwr_calibrate_rate;
	u8 rf_antenna_option;		/* 0xc9 */
	u8 rfe_option;
	u8 country_code[2];
	u8 res[3];
	struct rtw8723xs_efuse s;
} __packed;

_Static_assert(sizeof(struct rtw_txpwr_idx) == 42, "efuse tx power layout");
_Static_assert(__builtin_offsetof(struct rtw8723x_efuse, channel_plan) == 0xb8,
	       "efuse layout");
_Static_assert(__builtin_offsetof(struct rtw8723x_efuse, s.mac_addr) == 0x11a,
	       "efuse layout");

static void rtw8723x_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_LDO_EFUSE_CTRL + 3);
	if (enable) {
		ldo_pwr &= ~BIT_MASK_LDO25_VOLTAGE;
		ldo_pwr |= (BIT_LDO25_VOLTAGE_V25 << 4) | BIT_LDO25_EN;
	} else {
		ldo_pwr &= ~BIT_LDO25_EN;
	}
	rtw_write8(rtwdev, REG_LDO_EFUSE_CTRL + 3, ldo_pwr);
}

static void rtw8723x_efuse_grant(struct rtw_dev *rtwdev, bool on)
{
	if (on) {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_ELDR);
		rtw_write16_set(rtwdev, REG_SYS_CLKR, BIT_LOADER_CLK_EN | BIT_ANA8M);
	} else {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);
	}
}

#define invalid_efuse_header(hdr1, hdr2) \
	((hdr1) == 0xff || (((hdr1) & 0x1f) == 0xf && (hdr2) == 0xff))
#define invalid_efuse_content(word_en, i) \
	(((word_en) & BIT(i)) != 0x0)
#define get_efuse_blk_idx_2_byte(hdr1, hdr2) \
	((((hdr2) & 0xf0) >> 1) | (((hdr1) >> 5) & 0x07))
#define get_efuse_blk_idx_1_byte(hdr1) \
	(((hdr1) & 0xf0) >> 4)
#define block_idx_to_logical_idx(blk_idx, i) \
	(((blk_idx) << 3) + ((i) << 1))

/* efuse header format
 *
 * | 7        5   4    0 | 7        4   3          0 | 15  8  7   0 |
 *   block[2:0]   0 1111   block[6:3]   word_en[3:0]   byte0  byte1
 * | header 1 (optional) |          header 2         |    word N    |
 *
 * word_en: 4 bits each word. 0 -> write; 1 -> not write
 * N: 1~4, depends on word_en
 */
static int rtw_dump_logical_efuse_map(struct rtw_dev *rtwdev, const u8 *phy_map,
				      u8 *log_map)
{
	u32 physical_size = RTW_EFUSE_PHYS_SIZE;
	u32 protect_size = RTW_EFUSE_PROTECT_SIZE;
	u32 logical_size = RTW_EFUSE_LOG_SIZE;
	u32 phy_idx, log_idx;
	u8 hdr1, hdr2;
	u8 blk_idx;
	u8 word_en;
	int i;

	(void)rtwdev;
	for (phy_idx = 0; phy_idx < physical_size - protect_size;) {
		hdr1 = phy_map[phy_idx];
		hdr2 = phy_map[phy_idx + 1];
		if (invalid_efuse_header(hdr1, hdr2))
			break;

		if ((hdr1 & 0x1f) == 0xf) {
			/* 2-byte header format */
			blk_idx = get_efuse_blk_idx_2_byte(hdr1, hdr2);
			word_en = hdr2 & 0xf;
			phy_idx += 2;
		} else {
			/* 1-byte header format */
			blk_idx = get_efuse_blk_idx_1_byte(hdr1);
			word_en = hdr1 & 0xf;
			phy_idx += 1;
		}

		for (i = 0; i < 4; i++) {
			if (invalid_efuse_content(word_en, i))
				continue;

			log_idx = block_idx_to_logical_idx(blk_idx, i);
			if (phy_idx + 1 > physical_size - protect_size ||
			    log_idx + 1 > logical_size)
				return -EINVAL;

			log_map[log_idx] = phy_map[phy_idx];
			log_map[log_idx + 1] = phy_map[phy_idx + 1];
			phy_idx += 2;
		}
	}
	return 0;
}

static int rtw_dump_physical_efuse_map(struct rtw_dev *rtwdev, u8 *map)
{
	u32 size = RTW_EFUSE_PHYS_SIZE;
	u32 efuse_ctl;
	u32 addr;
	u32 cnt;

	rtw8723x_efuse_grant(rtwdev, true);
	rtw_write32_mask(rtwdev, REG_LDO_EFUSE_CTRL, BIT_MASK_EFUSE_BANK_SEL,
			 RTW_EFUSE_BANK_WIFI);

	/* disable 2.5V LDO */
	rtw8723x_cfg_ldo25(rtwdev, false);

	efuse_ctl = rtw_read32(rtwdev, REG_EFUSE_CTRL);

	for (addr = 0; addr < size; addr++) {
		efuse_ctl &= ~(BIT_MASK_EF_DATA | BITS_EF_ADDR);
		efuse_ctl |= (addr & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR;
		rtw_write32(rtwdev, REG_EFUSE_CTRL, efuse_ctl & (~BIT_EF_FLAG));

		cnt = 100000;
		do {
			efuse_ctl = rtw_read32(rtwdev, REG_EFUSE_CTRL);
			if (--cnt == 0)
				return -EBUSY;
		} while (!(efuse_ctl & BIT_EF_FLAG));

		*(map + addr) = (u8)(efuse_ctl & BIT_MASK_EF_DATA);
	}

	rtw8723x_efuse_grant(rtwdev, false);

	return 0;
}

/* Default power index table for RTL8703B/RTL8723D, used if EFUSE does
 * not contain valid data. Replaces EFUSE data from offset 0x10 (start
 * of txpwr_idx_table).
 */
static const u8 rtw8723x_txpwr_idx_table[] = {
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02
};

static void rtw8723x_read_efuse(struct rtw_dev *rtwdev, const u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 *pwr = (u8 *)efuse->txpwr_idx_table;
	const struct rtw8723x_efuse *map = (const void *)log_map;
	bool valid = false;
	u32 i;

	efuse->rfe_option = 0;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->pa_type_2g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g[0];
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter = map->thermal_meter;
	efuse->afe = map->afe;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	memcpy(efuse->addr, map->s.mac_addr, ETH_ALEN);

	/* If TX power index table in EFUSE is invalid, fall back to
	 * built-in table.
	 */
	for (i = 0; i < ARRAY_SIZE(rtw8723x_txpwr_idx_table); i++)
		if (pwr[i] != 0xff) {
			valid = true;
			break;
		}
	if (!valid) {
		for (i = 0; i < ARRAY_SIZE(rtw8723x_txpwr_idx_table); i++)
			pwr[i] = rtw8723x_txpwr_idx_table[i];
		rtw_info(rtwdev, "efuse TX power table empty, using defaults\n");
	}

	/* Override invalid antenna settings. */
	if (efuse->bt_setting == 0xff) {
		/* shared antenna */
		efuse->bt_setting |= BIT(0);
		/* RF path A */
		efuse->bt_setting &= ~BIT(6);
	}

	/* Override invalid board options: The coex code incorrectly
	 * assumes that if bits 6 & 7 are set the board doesn't
	 * support coex. Regd is also derived from rf_board_option and
	 * should be 0 if there's no valid data.
	 */
	if (efuse->rf_board_option == 0xff) {
		efuse->regd = 0;
		efuse->rf_board_option &= GENMASK(5, 0);
	}

	/* Override invalid crystal cap setting, default comes from
	 * vendor driver. Chip specific.
	 */
	if (efuse->crystal_cap == 0xff)
		efuse->crystal_cap = 0x20;

	/* from rtw_chip_efuse_info_setup() */
	if (efuse->pa_type_2g == 0xff)
		efuse->pa_type_2g = 0;
	if (efuse->lna_type_2g == 0xff)
		efuse->lna_type_2g = 0;
	if (efuse->channel_plan == 0xff)
		efuse->channel_plan = 0x7f;
	if (efuse->bt_setting & BIT(0))
		efuse->share_ant = true;
	efuse->btcoex = (efuse->rf_board_option & 0xe0) == 0x20;
}

int rtw_parse_efuse_map(struct rtw_dev *rtwdev)
{
	static u8 phy_map[RTW_EFUSE_PHYS_SIZE];
	u8 *log_map = rtwdev->efuse_log;
	int ret;

	ret = rtw_dump_physical_efuse_map(rtwdev, phy_map);
	if (ret) {
		rtw_err(rtwdev, "failed to dump efuse physical map\n");
		return ret;
	}

	memset(log_map, 0xff, RTW_EFUSE_LOG_SIZE);
	ret = rtw_dump_logical_efuse_map(rtwdev, phy_map, log_map);
	if (ret) {
		rtw_err(rtwdev, "failed to dump efuse logical map\n");
		return ret;
	}

	rtw8723x_read_efuse(rtwdev, log_map);
	return 0;
}
