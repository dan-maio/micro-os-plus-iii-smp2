/* rtw_trx.c - RTL8723DS data path over SDIO: TX descriptor + TX FIFO write,
 * RX FIFO read + RX descriptor parsing, and firmware (H2C) commands.
 * Ported from Linux rtw88 (tx.c, rx.c, sdio.c, fw.c, rtw8723d.c).
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation
 * Copyright (C) 2021 Martin Blumenstingl, Jernej Skrabec (SDIO)
 */
#include "rtw.h"

/* ------------------------------------------------------------------- TX */
struct rtw_tx_desc {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	__le32 w9;
} __packed;

#define RTW_TX_DESC_W0_TXPKTSIZE GENMASK(15, 0)
#define RTW_TX_DESC_W0_OFFSET GENMASK(23, 16)
#define RTW_TX_DESC_W0_BMC BIT(24)
#define RTW_TX_DESC_W0_LS BIT(26)
#define RTW_TX_DESC_W0_DISQSELSEQ BIT(31)
#define RTW_TX_DESC_W1_MACID GENMASK(7, 0)
#define RTW_TX_DESC_W1_QSEL GENMASK(12, 8)
#define RTW_TX_DESC_W1_RATE_ID GENMASK(20, 16)
#define RTW_TX_DESC_W3_USE_RATE BIT(8)
#define RTW_TX_DESC_W3_DISDATAFB BIT(10)
#define RTW_TX_DESC_W4_DATARATE GENMASK(6, 0)
#define RTW_TX_DESC_W4_DATARATE_FB_LIMIT GENMASK(12, 8)
#define RTW_TX_DESC_W7_TXDESC_CHECKSUM GENMASK(15, 0)
#define RTW_TX_DESC_W8_EN_HWSEQ BIT(15)

#define TX_DESC_QSEL_TID0	0
#define TX_DESC_QSEL_MGMT	18
#define TX_DESC_QSEL_H2C	19

#define RTW_RATEID_BG		6
#define RTW_RATEID_B_20M	8

_Static_assert(sizeof(struct rtw_tx_desc) == RTW_TX_DESC_SIZE, "TX descriptor");

static void rtw8723x_fill_txdesc_checksum(u8 *txdesc)
{
	struct rtw_tx_desc *tx_desc = (struct rtw_tx_desc *)txdesc;
	size_t words = 32 / 2; /* calculate the first 32 bytes (16 words) */
	u16 chksum = 0;
	u16 word;

	tx_desc->w7 &= ~RTW_TX_DESC_W7_TXDESC_CHECKSUM;

	while (words--) {
		memcpy(&word, txdesc, 2);
		chksum ^= word;
		txdesc += 2;
	}

	chksum = ~chksum;

	tx_desc->w7 |= FIELD_PREP(RTW_TX_DESC_W7_TXDESC_CHECKSUM, chksum);
}

/* Pages available to a queue = its own pool + the public pool. */
static int rtw_sdio_check_free_txpg(struct rtw_dev *rtwdev,
				    enum rtw_tx_queue queue, size_t count)
{
	unsigned int pages_free, pages_needed;
	u32 free_txpg;

	free_txpg = rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);

	switch (queue) {
	case RTW_QUEUE_H2C:
	case RTW_QUEUE_MGMT:
		/* high */
		pages_free = free_txpg & 0xff;
		break;
	default:
		/* low */
		pages_free = (free_txpg >> 16) & 0xff;
		break;
	}

	/* add the pages from the public queue */
	pages_free += (free_txpg >> 24) & 0xff;

	pages_needed = DIV_ROUND_UP(count, TX_PAGE_SIZE);

	return pages_needed > pages_free ? -EBUSY : 0;
}

int rtw_tx_frame(struct rtw_dev *rtwdev, u8 *buf, u32 len,
		 const struct rtw_tx_info *info)
{
	struct rtw_tx_desc *tx_desc = (struct rtw_tx_desc *)buf;
	u32 total = RTW_TX_DESC_SIZE + len;
	size_t write_size;
	u32 txaddr;
	u8 qsel;
	int tries;

	memset(tx_desc, 0, sizeof(*tx_desc));

	switch (info->queue) {
	case RTW_QUEUE_MGMT:
		qsel = TX_DESC_QSEL_MGMT;
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK, REG_SDIO_CMD_ADDR_TXFF_EXTRA);
		break;
	case RTW_QUEUE_H2C:
		qsel = TX_DESC_QSEL_H2C;
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK, REG_SDIO_CMD_ADDR_TXFF_HIGH);
		break;
	default:
		qsel = TX_DESC_QSEL_TID0;
		txaddr = FIELD_PREP(REG_SDIO_CMD_ADDR_MSK, REG_SDIO_CMD_ADDR_TXFF_LOW);
		break;
	}
	txaddr += DIV_ROUND_UP(total, 4);

	tx_desc->w0 = FIELD_PREP(RTW_TX_DESC_W0_TXPKTSIZE, len) |
		      FIELD_PREP(RTW_TX_DESC_W0_OFFSET, RTW_TX_DESC_SIZE) |
		      FIELD_PREP(RTW_TX_DESC_W0_BMC, info->bmc) |
		      FIELD_PREP(RTW_TX_DESC_W0_LS, 1);
	tx_desc->w1 = FIELD_PREP(RTW_TX_DESC_W1_MACID, info->mac_id) |
		      FIELD_PREP(RTW_TX_DESC_W1_QSEL, qsel);
	tx_desc->w4 = FIELD_PREP(RTW_TX_DESC_W4_DATARATE, info->rate) |
		      FIELD_PREP(RTW_TX_DESC_W4_DATARATE_FB_LIMIT, 0x1f);

	if (info->fixed_rate) {
		/* management-style: one fixed rate, no fallback */
		tx_desc->w1 |= FIELD_PREP(RTW_TX_DESC_W1_RATE_ID, RTW_RATEID_B_20M);
		tx_desc->w3 = FIELD_PREP(RTW_TX_DESC_W3_USE_RATE, 1) |
			      FIELD_PREP(RTW_TX_DESC_W3_DISDATAFB, 1);
	} else {
		/* firmware rate adaptation, starting from info->rate */
		tx_desc->w1 |= FIELD_PREP(RTW_TX_DESC_W1_RATE_ID, RTW_RATEID_BG);
	}
	if (info->hw_seq) {
		tx_desc->w0 |= FIELD_PREP(RTW_TX_DESC_W0_DISQSELSEQ, 1);
		tx_desc->w8 = FIELD_PREP(RTW_TX_DESC_W8_EN_HWSEQ, 1);
	}

	rtw8723x_fill_txdesc_checksum(buf);

	write_size = plat_sdio_align_size(total);
	if (write_size > total)
		memset(buf + total, 0, write_size - total);

	for (tries = 0; rtw_sdio_check_free_txpg(rtwdev, info->queue, total); tries++) {
		if (tries >= 200) {
			rtwdev->tx_busy++;
			return -EBUSY;
		}
		udelay(500);
	}

	if (plat_sdio_write(txaddr, buf, write_size)) {
		rtw_warn(rtwdev, "TX: SDIO write of %u bytes failed\n", (u32)write_size);
		return -EIO;
	}
	rtwdev->tx_count++;
	return 0;
}

/* ------------------------------------------------------------------- RX */
struct rtw_rx_desc {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
} __packed;

#define RTW_RX_DESC_W0_PKT_LEN		GENMASK(13, 0)
#define RTW_RX_DESC_W0_CRC32		BIT(14)
#define RTW_RX_DESC_W0_ICV_ERR		BIT(15)
#define RTW_RX_DESC_W0_DRV_INFO_SIZE	GENMASK(19, 16)
#define RTW_RX_DESC_W0_ENC_TYPE		GENMASK(22, 20)
#define RTW_RX_DESC_W0_SHIFT		GENMASK(25, 24)
#define RTW_RX_DESC_W0_PHYST		BIT(26)
#define RTW_RX_DESC_W0_SWDEC		BIT(27)
#define RTW_RX_DESC_W2_C2H		BIT(28)
#define RTW_RX_DESC_W3_RX_RATE		GENMASK(6, 0)

#define FCS_LEN 4

_Static_assert(sizeof(struct rtw_rx_desc) == RTW_RX_DESC_SIZE, "RX descriptor");

u32 rtw_rx_parse(struct rtw_dev *rtwdev, const u8 *desc, u32 remaining,
		 struct rtw_rx_info *info, const u8 **frame)
{
	struct rtw_rx_desc rx_desc;
	const u8 *phy_status;
	u32 pkt_offset, curr_pkt_len;
	u32 enc_type, swdec;

	if (remaining < RTW_RX_DESC_SIZE)
		return 0;
	memcpy(&rx_desc, desc, sizeof(rx_desc));
	memset(info, 0, sizeof(*info));

	info->pkt_len = FIELD_GET(RTW_RX_DESC_W0_PKT_LEN, rx_desc.w0);
	info->crc_err = FIELD_GET(RTW_RX_DESC_W0_CRC32, rx_desc.w0);
	info->icv_err = FIELD_GET(RTW_RX_DESC_W0_ICV_ERR, rx_desc.w0);
	info->drv_info_sz = FIELD_GET(RTW_RX_DESC_W0_DRV_INFO_SIZE, rx_desc.w0);
	enc_type = FIELD_GET(RTW_RX_DESC_W0_ENC_TYPE, rx_desc.w0);
	info->shift = FIELD_GET(RTW_RX_DESC_W0_SHIFT, rx_desc.w0);
	info->phy_status = FIELD_GET(RTW_RX_DESC_W0_PHYST, rx_desc.w0);
	swdec = FIELD_GET(RTW_RX_DESC_W0_SWDEC, rx_desc.w0);
	info->decrypted = !swdec && enc_type != 0;
	info->is_c2h = FIELD_GET(RTW_RX_DESC_W2_C2H, rx_desc.w2);
	info->rate = FIELD_GET(RTW_RX_DESC_W3_RX_RATE, rx_desc.w3);

	if (info->pkt_len == 0)
		return 0;
	if (info->drv_info_sz && info->drv_info_sz != PHY_STATUS_SIZE)
		return 0;
	if (info->phy_status && !info->drv_info_sz)
		return 0;

	/* drv_info_sz is in unit of 8-bytes */
	info->drv_info_sz *= 8;

	pkt_offset = RTW_RX_DESC_SIZE + info->drv_info_sz + info->shift;
	curr_pkt_len = ALIGN(pkt_offset + info->pkt_len, RTW_SDIO_DATA_PTR_ALIGN);
	if (pkt_offset + info->pkt_len > remaining)
		return 0;

	*frame = desc + pkt_offset;

	if (info->is_c2h)
		return curr_pkt_len;

	if (info->pkt_len <= FCS_LEN)
		return 0;
	info->pkt_len -= FCS_LEN;	/* RCR.APP_FCS keeps the FCS in the buffer */

	if (info->phy_status) {
		/* query_phy_status(): page 0 = CCK, page 1 = OFDM/HT */
		phy_status = desc + RTW_RX_DESC_SIZE + info->shift;
		if ((phy_status[0] & 0xf) == 0)
			info->rssi_dbm = (s8)(phy_status[1] - 97);
		else
			info->rssi_dbm = (s8)(phy_status[1] - 110);
	} else {
		info->rssi_dbm = -128;
	}
	(void)rtwdev;
	return curr_pkt_len;
}

int rtw_rx_poll(struct rtw_dev *rtwdev, u8 *buf, u32 buf_size)
{
	size_t bufsz;
	u32 rx_len;
	u32 hisr;

	hisr = rtw_read32(rtwdev, REG_SDIO_HISR);
	if (hisr & REG_SDIO_HISR_TXERR) {
		u32 val = rtw_read32(rtwdev, REG_TXDMA_STATUS);

		rtwdev->tx_dma_errors++;
		rtwdev->tx_dma_status |= val;
		rtw_write32(rtwdev, REG_TXDMA_STATUS, val);
	}
	if (!(hisr & REG_SDIO_HISR_RX_REQUEST)) {
		if (hisr)
			rtw_write32(rtwdev, REG_SDIO_HISR, hisr);
		return 0;
	}

	rx_len = rtw_read16(rtwdev, REG_SDIO_RX0_REQ_LEN);
	hisr &= ~REG_SDIO_HISR_RX_REQUEST;
	if (hisr)
		rtw_write32(rtwdev, REG_SDIO_HISR, hisr);
	if (!rx_len)
		return 0;

	bufsz = plat_sdio_align_size(rx_len);
	if (bufsz > buf_size) {
		rtw_warn(rtwdev, "RX: %u bytes do not fit the buffer\n", rx_len);
		rtwdev->rx_errors++;
		return 0;
	}

	if (plat_sdio_read(RTW_SDIO_ADDR_RX_RX0FF_GEN(rtwdev->rx_addr++), buf, bufsz)) {
		rtwdev->rx_errors++;
		return 0;
	}
	rtwdev->rx_count++;
	return (int)rx_len;
}

void rtw_hci_start(struct rtw_dev *rtwdev)
{
	/* rtw_sdio_enable_rx_aggregation(), RTL8723D values */
	rtw_write8_set(rtwdev, REG_RXDMA_AGG_PG_TH + 3, BIT(7));
	rtw_write32_set(rtwdev, REG_RXDMA_AGG_PG_TH, BIT_EN_PRE_CALC);
	rtw_write8_set(rtwdev, REG_TXDMA_PQ_MAP, BIT_RXDMA_AGG_EN);
	rtw_write16(rtwdev, REG_RXDMA_AGG_PG_TH,
		    FIELD_PREP(BIT_RXDMA_AGG_PG_TH, 0xa) |
		    FIELD_PREP(BIT_DMA_AGG_TO_V1, 0x3));
	rtw_write8_set(rtwdev, REG_RXDMA_MODE, BIT_DMA_MODE);

	/* The host polls HISR; the interrupt line itself is not used. */
	rtw_write32(rtwdev, REG_SDIO_HIMR, REG_SDIO_HIMR_RX_REQUEST);
	rtwdev->rx_addr = 0;
}

/* ------------------------------------------------- firmware commands */
#define H2C_CMD_MEDIA_STATUS_RPT	0x01
#define H2C_CMD_RA_INFO			0x40
#define H2C_CMD_RSSI_MONITOR		0x42

int rtw_fw_send_h2c(struct rtw_dev *rtwdev, const u8 *h2c)
{
	static const u32 box_regs[4] = {
		REG_HMEBOX0, REG_HMEBOX1, REG_HMEBOX2, REG_HMEBOX3
	};
	static const u32 box_ex_regs[4] = {
		REG_HMEBOX0_EX, REG_HMEBOX1_EX, REG_HMEBOX2_EX, REG_HMEBOX3_EX
	};
	u8 box = rtwdev->h2c_box;
	u32 msg, msg_ext;
	int tries;

	for (tries = 0; (rtw_read8(rtwdev, REG_HMETFR) >> box) & 0x1; tries++) {
		if (tries >= 30) {
			rtw_err(rtwdev, "failed to send h2c command 0x%02x\n", h2c[0]);
			return -EBUSY;
		}
		udelay(100);
	}

	memcpy(&msg, h2c, 4);
	memcpy(&msg_ext, h2c + 4, 4);
	rtw_write32(rtwdev, box_ex_regs[box], le32_to_cpu(msg_ext));
	rtw_write32(rtwdev, box_regs[box], le32_to_cpu(msg));

	if (++rtwdev->h2c_box >= 4)
		rtwdev->h2c_box = 0;
	return 0;
}

void rtw_fw_media_status(struct rtw_dev *rtwdev, u8 mac_id, bool connect)
{
	u32 w[2] = {0, 0};

	w[0] = H2C_CMD_MEDIA_STATUS_RPT | FIELD_PREP(BIT(8), connect) |
	       FIELD_PREP(GENMASK(23, 16), mac_id);
	rtw_fw_send_h2c(rtwdev, (const u8 *)w);
}

void rtw_fw_send_ra_info(struct rtw_dev *rtwdev, u8 mac_id, u32 rate_mask,
			 u8 rssi_level)
{
	u32 w[2] = {0, 0};

	w[0] = H2C_CMD_RA_INFO |
	       FIELD_PREP(GENMASK(15, 8), mac_id) |
	       FIELD_PREP(GENMASK(20, 16), RTW_RATEID_BG) |
	       FIELD_PREP(GENMASK(22, 21), rssi_level) |	/* init RA level */
	       FIELD_PREP(BIT(30), 1);				/* disable power training */
	w[1] = rate_mask;
	rtw_fw_send_h2c(rtwdev, (const u8 *)w);
}

void rtw_fw_send_rssi_info(struct rtw_dev *rtwdev, u8 mac_id, u8 rssi)
{
	u32 w[2] = {0, 0};

	w[0] = H2C_CMD_RSSI_MONITOR | FIELD_PREP(GENMASK(15, 8), mac_id) |
	       FIELD_PREP(GENMASK(31, 24), rssi);
	rtw_fw_send_h2c(rtwdev, (const u8 *)w);
}
