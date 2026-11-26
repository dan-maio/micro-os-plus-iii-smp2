/* rtw_core.c - bring-up order of the RTL8723DS (rtw88 main.c: rtw_power_on,
 * rtw_core_start) and the few MAC registers a station needs afterwards
 * (rtw88 mac80211.c: add_interface, bss_info_changed, configure_filter).
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation
 */
#include "rtw_8723x.h"

struct rtw_dev rtw;
int rtw_debug;

/* hardware port 0 */
#define REG_PORT0_MACID		0x0610
#define REG_PORT0_BSSID		0x0618
#define REG_PORT0_NET_TYPE	0x0100
#define MASK_PORT0_NET_TYPE	0x30000
#define REG_PORT0_AID		0x06a8
#define MASK_PORT0_AID		0x7ff
#define REG_BCN_INTERVAL_P0	0x0554

#define RTW_NET_NO_LINK		0
#define RTW_NET_MGD_LINKED	2

#define RTW_RCR_DEFAULT	(BIT_APP_FCS | BIT_APP_MIC | BIT_APP_ICV | \
			 BIT_HTC_LOC_CTRL | BIT_APP_PHYSTS | BIT_AB | BIT_AM | \
			 BIT_APM | BIT_VHT_DACK)

static void rtw_write_addr(struct rtw_dev *rtwdev, u32 start, const u8 *addr)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		rtw_write8(rtwdev, start + i, addr[i]);
}

void rtw_set_mac_addr(struct rtw_dev *rtwdev, const u8 *addr)
{
	rtw_write_addr(rtwdev, REG_PORT0_MACID, addr);
}

void rtw_set_bssid(struct rtw_dev *rtwdev, const u8 *bssid)
{
	rtw_write_addr(rtwdev, REG_PORT0_BSSID, bssid);
}

/* bss_only: accept beacons/probe responses and data of our BSS only. */
u32 rtw_debug_tx_dma_errors(void) { return rtw.tx_dma_errors; }

void rtw_set_rx_filter(struct rtw_dev *rtwdev, bool bss_only)
{
	if (bss_only)
		rtwdev->rcr |= BIT_CBSSID_BCN | BIT_CBSSID_DATA;
	else
		rtwdev->rcr &= ~(BIT_CBSSID_BCN | BIT_CBSSID_DATA);
	rtw_write32(rtwdev, REG_RCR, rtwdev->rcr);
}

void rtw_set_linked(struct rtw_dev *rtwdev, bool linked, u16 aid,
		    u16 beacon_interval)
{
	rtw_write32_mask(rtwdev, REG_PORT0_NET_TYPE, MASK_PORT0_NET_TYPE,
			 linked ? RTW_NET_MGD_LINKED : RTW_NET_NO_LINK);
	rtw_write32_mask(rtwdev, REG_PORT0_AID, MASK_PORT0_AID, linked ? aid : 0);
	if (linked && beacon_interval)
		rtw_write16(rtwdev, REG_BCN_INTERVAL_P0, beacon_interval);
	rtw_set_rx_filter(rtwdev, linked);
	rtw_fw_media_status(rtwdev, 0, linked);
	if (linked) {
		/* 802.11b/g rates: CCK 1..11 (bits 0-3) + OFDM 6..54 (bits 4-11) */
		rtw_fw_send_ra_info(rtwdev, 0, 0x00000fff, 0);
	}
}

int rtw_core_init(void)
{
	struct rtw_dev *rtwdev = &rtw;
	const u8 *mac;
	int ret;

	memset(rtwdev, 0, sizeof(*rtwdev));
	rtw_io_errors = 0;

	/* ---- rtw_chip_efuse_info_setup(): power on, firmware, efuse ---- */
	ret = rtw_mac_power_on(rtwdev);
	if (ret)
		return ret;
	rtw_info(rtwdev, "MAC powered on, SYS_CFG1=0x%08x cut=%u\n",
		 rtwdev->chip_version, rtwdev->cut_version);

	ret = rtw_download_firmware(rtwdev, rtw8723d_fw_start,
				    (u32)(rtw8723d_fw_end - rtw8723d_fw_start));
	if (ret) {
		rtw_err(rtwdev, "firmware download failed (%d)\n", ret);
		return ret;
	}
	rtw_info(rtwdev, "firmware v%u.%u running\n", rtwdev->fw_version,
		 rtwdev->fw_sub_version);

	ret = rtw_parse_efuse_map(rtwdev);
	if (ret)
		return ret;
	mac = rtwdev->efuse.addr;
	rtw_info(rtwdev, "efuse: MAC %02x:%02x:%02x:%02x:%02x:%02x xtal=0x%02x "
		 "board=0x%02x bt=0x%02x plan=0x%02x\n",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		 rtwdev->efuse.crystal_cap, rtwdev->efuse.rf_board_option,
		 rtwdev->efuse.bt_setting, rtwdev->efuse.channel_plan);

	/* ---- rtw_power_on(): MAC, PHY, bus ---- */
	ret = rtw_mac_init(rtwdev);
	if (ret) {
		rtw_err(rtwdev, "failed to configure mac (%d)\n", ret);
		return ret;
	}

	rtw8723d_phy_set_param(rtwdev);

	/* mac_postinit */
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);

	rtw_hci_start(rtwdev);
	rtw_coex_wifi_only(rtwdev);

	/* ---- rtw_core_start() / add_interface(station) ---- */
	rtwdev->rcr = RTW_RCR_DEFAULT;
	rtw_write32(rtwdev, REG_RCR, rtwdev->rcr);

	rtw_set_mac_addr(rtwdev, rtwdev->efuse.addr);
	rtw_write32_mask(rtwdev, REG_PORT0_NET_TYPE, MASK_PORT0_NET_TYPE,
			 RTW_NET_NO_LINK);
	rtw_write8(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* EDCA for a non-QoS station: AIFS = 2 slots + SIFS (DIFS), CW 15..1023 */
	rtw_write32(rtwdev, REG_EDCA_BE_PARAM, 0x0000a41c);
	rtw_write32(rtwdev, REG_EDCA_BK_PARAM, 0x0000a41c);
	rtw_write32(rtwdev, REG_EDCA_VI_PARAM, 0x0000a41c);
	rtw_write32(rtwdev, REG_EDCA_VO_PARAM, 0x0000a41c);

	rtw8723d_set_channel(rtwdev, 1);

	rtw_info(rtwdev, "RF 0x00=0x%05x 0x18=0x%05x, TX power idx CCK %u OFDM54 %u\n",
		 rtw_read_rf(rtwdev, RF_PATH_A, 0x00, RFREG_MASK),
		 rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK),
		 rtwdev->tx_pwr_tbl[DESC_RATE1M], rtwdev->tx_pwr_tbl[DESC_RATE54M]);

	if (rtw_io_errors)
		rtw_warn(rtwdev, "%u SDIO I/O errors during init\n", rtw_io_errors);
	return 0;
}

void rtw_core_shutdown(void)
{
	if (rtw.powered)
		rtw_mac_power_off(&rtw);
}
