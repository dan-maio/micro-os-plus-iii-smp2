/** @file flash-prog-pico2.cpp @brief See flash-prog.h. RP2350 flat-fs backend.
 *
 * The predecessor's xip-glue/src/flash-prog-pico2.c (micro-os-plus-iii-pico2),
 * unchanged but for C++: the flat-fs's flash operations over the board's
 * RAM-resident writer (mu_flash_*, test/boards/pico2/src/flash_rp2350.cpp),
 * a 256-byte read-modify-write per page. */
#include "flash-prog.h"
#include "flash_rp2350.h"     /* mu_flash_erase / mu_flash_program / mu_flash_xip_flush */
#include <string.h>
#include <stdint.h>

#define XIP_BASE   0x10000000u
#define BLOCK_SIZE 0x10000u    /* 64 KB erase block */
#define PAGE_SIZE  256u        /* RP2350 program page */

/* FLASH_FS base address (linker symbol; its ADDRESS is the value). */
extern "C" uint8_t _fs_base[];

extern "C" void flash_unlock(void) { /* no-op on RP2350 */ }
extern "C" void flash_lock(void)   { /* no-op on RP2350 */ }

extern "C" int flash_erase_sector(uint32_t snb)
{
    uint32_t base_off = (uint32_t)(uintptr_t)_fs_base - XIP_BASE;
    mu_flash_erase(base_off + snb * BLOCK_SIZE, BLOCK_SIZE);
    mu_flash_xip_flush();
    return 0;
}

/* One 256-byte staging page in RAM (provisioning is single-threaded, core 0). */
static uint8_t g_page[PAGE_SIZE];

extern "C" int flash_program(uint32_t addr, const void* src, uint32_t len)
{
    const uint8_t* s = (const uint8_t*)src;
    uint32_t done = 0;
    while (done < len) {
        uint32_t cur       = addr + done;
        uint32_t page_addr = cur & ~(PAGE_SIZE - 1u);         /* page base       */
        uint32_t in_page   = cur - page_addr;                 /* offset in page  */
        uint32_t n         = PAGE_SIZE - in_page;             /* bytes this page */
        if (n > len - done) n = len - done;

        /* Read-modify-write: current page (XIP) -> overlay new bytes -> program. */
        memcpy(g_page, (const void*)(uintptr_t)page_addr, PAGE_SIZE);
        memcpy(g_page + in_page, s + done, n);
        mu_flash_program(page_addr - XIP_BASE, g_page, PAGE_SIZE);

        done += n;
    }
    mu_flash_xip_flush();
    return 0;
}

extern "C" void flash_cache_flush(void) { mu_flash_xip_flush(); }
