/* rtw_io.c - RTL8723DS register access over SDIO (from rtw88 sdio.c).
 *
 * The chip's 32-bit I/O address space is reached through SDIO function 1:
 * bits 16:13 of the CMD52/CMD53 address select the domain (SDIO local
 * registers, MAC registers, TX FIFOs, RX FIFO), the low bits the offset.
 * Until the MAC is powered on only CMD52 single-byte access works, so 16/32
 * bit accesses are split into bytes; afterwards aligned 32-bit accesses use
 * one CMD53.
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 */
#include "rtw.h"

u32 rtw_io_errors;

static bool rtw_sdio_is_bus_addr(u32 addr)
{
	return !!(addr & RTW_SDIO_BUS_MSK);
}

static u32 rtw_sdio_to_bus_offset(u32 addr)
{
	switch (addr & RTW_SDIO_BUS_MSK) {
	case WLAN_IOREG_OFFSET:
		addr &= WLAN_IOREG_REG_MSK;
		addr |= FIELD_PREP(REG_SDIO_CMD_ADDR_MSK, REG_SDIO_CMD_ADDR_MAC_REG);
		break;
	case SDIO_LOCAL_OFFSET:
		addr &= SDIO_LOCAL_REG_MSK;
		addr |= FIELD_PREP(REG_SDIO_CMD_ADDR_MSK, REG_SDIO_CMD_ADDR_SDIO_REG);
		break;
	default:
		rtw_warn(NULL, "cannot convert addr 0x%08x to bus offset\n", addr);
	}
	return addr;
}

static u32 rtw_sdio_to_io_address(u32 addr)
{
	if (!rtw_sdio_is_bus_addr(addr))
		addr |= WLAN_IOREG_OFFSET;
	return rtw_sdio_to_bus_offset(addr);
}

static void io_error(const char *what, u32 addr)
{
	if (rtw_io_errors++ < 8)
		rtw_warn(NULL, "sdio %s failed (0x%x)\n", what, addr);
}

u8 rtw_read8(struct rtw_dev *rtwdev, u32 addr)
{
	u8 val = 0;

	(void)rtwdev;
	addr = rtw_sdio_to_io_address(addr);
	if (plat_sdio_readb(addr, &val))
		io_error("read8", addr);
	return val;
}

void rtw_write8(struct rtw_dev *rtwdev, u32 addr, u8 val)
{
	(void)rtwdev;
	addr = rtw_sdio_to_io_address(addr);
	if (plat_sdio_writeb(addr, val))
		io_error("write8", addr);
}

u16 rtw_read16(struct rtw_dev *rtwdev, u32 addr)
{
	u8 buf[2] = {0, 0};
	int i;

	(void)rtwdev;
	addr = rtw_sdio_to_io_address(addr);
	for (i = 0; i < 2; i++) {
		if (plat_sdio_readb(addr + i, &buf[i])) {
			io_error("read16", addr);
			return 0;
		}
	}
	return (u16)(buf[0] | (buf[1] << 8));
}

void rtw_write16(struct rtw_dev *rtwdev, u32 addr, u16 val)
{
	int i;

	(void)rtwdev;
	addr = rtw_sdio_to_io_address(addr);
	for (i = 0; i < 2; i++) {
		if (plat_sdio_writeb(addr + i, (u8)(val >> (8 * i)))) {
			io_error("write16", addr);
			return;
		}
	}
}

u32 rtw_read32(struct rtw_dev *rtwdev, u32 addr)
{
	u32 val = 0;
	u8 byte;
	int i;

	addr = rtw_sdio_to_io_address(addr);
	if (rtwdev->powered && IS_ALIGNED(addr, 4)) {
		u32 word __attribute__((aligned(4))) = 0;

		if (plat_sdio_read(addr, &word, 4))
			io_error("read32", addr);
		return le32_to_cpu(word);
	}
	for (i = 0; i < 4; i++) {
		if (plat_sdio_readb(addr + i, &byte)) {
			io_error("read32", addr);
			return 0;
		}
		val |= (u32)byte << (8 * i);
	}
	return val;
}

void rtw_write32(struct rtw_dev *rtwdev, u32 addr, u32 val)
{
	int i;

	addr = rtw_sdio_to_io_address(addr);
	if (rtwdev->powered && IS_ALIGNED(addr, 4)) {
		u32 word __attribute__((aligned(4))) = cpu_to_le32(val);

		if (plat_sdio_write(addr, &word, 4))
			io_error("write32", addr);
		return;
	}
	for (i = 0; i < 4; i++) {
		if (plat_sdio_writeb(addr + i, (u8)(val >> (8 * i)))) {
			io_error("write32", addr);
			return;
		}
	}
}

bool check_hw_ready(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target)
{
	u32 cnt;

	for (cnt = 0; cnt < 1000; cnt++) {
		if (rtw_read32_mask(rtwdev, addr, mask) == target)
			return true;
		udelay(10);
	}
	return false;
}
