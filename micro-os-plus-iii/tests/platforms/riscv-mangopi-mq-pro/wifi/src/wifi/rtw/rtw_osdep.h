/* rtw_osdep.h - the small piece of Linux the rtw88 code expects, mapped onto
 * the bare-metal platform (no OS, no libc).
 *
 * SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 */
#ifndef MQPRO_RTW_OSDEP_H
#define MQPRO_RTW_OSDEP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* The D1 is little endian, like every register and descriptor of the chip. */
typedef u16 __le16;
typedef u32 __le32;
#define cpu_to_le16(x) ((u16)(x))
#define cpu_to_le32(x) ((u32)(x))
#define le16_to_cpu(x) ((u16)(x))
#define le32_to_cpu(x) ((u32)(x))
#define __LITTLE_ENDIAN 1

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#define BIT(n) (1u << (n))
#define GENMASK(h, l) ((~0u << (l)) & (~0u >> (31 - (h))))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define __ffs(x) ((u32)__builtin_ctz(x))
#define FIELD_PREP(mask, val) (((u32)(val) << __builtin_ctz(mask)) & (u32)(mask))
#define FIELD_GET(mask, reg) (((u32)(reg) & (u32)(mask)) >> __builtin_ctz(mask))
#define u32_get_bits(v, mask) FIELD_GET(mask, v)
#define u8_get_bits(v, mask) ((u8)FIELD_GET(mask, v))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#ifndef __cplusplus
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif

static inline s32 sign_extend32(u32 value, int index)
{
	u8 shift = 31 - index;
	return (s32)(value << shift) >> shift;
}

#define ETH_ALEN 6

#define EINVAL 22
#define EBUSY 16
#define EALREADY 114
#define ETIMEDOUT 110
#define EIO 5
#define ENOMEM 12

/* ---- platform services, implemented in wifi/platform.cpp ---- */
u64 plat_time_us(void);
void plat_delay_us(u32 us);     /* long delays keep USB serviced */
void kprintf(const char *format, ...) __attribute__((format(printf, 1, 2)));

#define udelay(us) plat_delay_us(us)
#define mdelay(ms) plat_delay_us((ms) * 1000u)
#define msleep(ms) plat_delay_us((ms) * 1000u)
#define usleep_range(lo, hi) plat_delay_us(lo)

#define rtw_err(dev, ...) kprintf("[RTW] error: " __VA_ARGS__)
#define rtw_warn(dev, ...) kprintf("[RTW] warning: " __VA_ARGS__)
#define rtw_info(dev, ...) kprintf("[RTW] " __VA_ARGS__)
extern int rtw_debug;
#define rtw_dbg(dev, mask, ...) \
	do { if (rtw_debug) kprintf("[RTW] " __VA_ARGS__); } while (0)

#ifndef __cplusplus
void *memcpy(void *dest, const void *src, size_t count);
void *memset(void *dest, int value, size_t count);
int memcmp(const void *lhs, const void *rhs, size_t count);
#endif

/* ---- SDIO bus, implemented in wifi/platform.cpp on top of wifi/sdio.cpp ---- */
int plat_sdio_readb(u32 addr, u8 *value);
int plat_sdio_writeb(u32 addr, u8 value);
int plat_sdio_read(u32 addr, void *buf, size_t len);   /* CMD53, 4-aligned */
int plat_sdio_write(u32 addr, const void *buf, size_t len);
size_t plat_sdio_align_size(size_t len);

#endif
