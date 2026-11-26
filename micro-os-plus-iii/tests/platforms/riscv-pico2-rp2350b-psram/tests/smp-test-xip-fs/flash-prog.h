/**
 * @file flash-prog.h  (pico2 / RP2350)
 * @brief The flat-fs (fs-archive.cpp) flash backend, RP2350 flavour.
 *
 * Same API the SCL STM32 flat-fs expects, implemented over the RP2350 bootrom
 * flash driver (mu_flash_*). The RP2350 programs in 256-byte pages, while the
 * append-log flat-fs issues small word-granular writes (superblock, TOC entries,
 * blobs). flash_program() bridges this with a 256-byte read-modify-write: read
 * the page through XIP, overlay the new bytes, program the whole page. This is
 * safe for the append-log because new bytes only ever land in still-erased
 * (0xFF) positions, so no bit needs to flip 0->1.
 *
 * Provisioning runs on core 0 with core 1 parked / not yet launched (a flash op
 * disables XIP for the whole window).
 */
#ifndef PICO2_FLASH_PROG_H_
#define PICO2_FLASH_PROG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "Sector" = a 64 KB erase block within FLASH_FS. fs_format() erases LO+HI
 * (128 KB), enough for the test archive; enlarge if the archive grows. */
#define FLASH_FS_SECTOR_LO 0u
#define FLASH_FS_SECTOR_HI 1u

/** @brief No-op on RP2350 (no CR unlock like the STM32). */
void flash_unlock(void);
/** @brief No-op on RP2350. */
void flash_lock(void);

/** @brief Erase 64 KB block @p snb of FLASH_FS. Returns 0 on success. */
int flash_erase_sector(uint32_t snb);

/** @brief Program @p len bytes from RAM @p src to absolute flash @p addr
 *         (0x101xxxxx), via 256-byte page read-modify-write. Returns 0 on ok. */
int flash_program(uint32_t addr, const void* src, uint32_t len);

/** @brief Invalidate the XIP cache so reads see freshly written bytes. */
void flash_cache_flush(void);

#ifdef __cplusplus
}
#endif
#endif /* PICO2_FLASH_PROG_H_ */
