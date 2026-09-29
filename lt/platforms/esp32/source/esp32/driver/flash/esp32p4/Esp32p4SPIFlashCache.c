/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32p4/Esp32p4SPIFlashCache.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Flash on this part is reached through the shared L2 cache, so the L2 is what
 * is suspended and resumed here - not an instruction cache, as on the esp32c3
 * and esp32s3.  The L1 caches in front of it also serve internal memory, which
 * is why the invalidate covers L1 DCache as well: a mapping change that only
 * reached L2 would still read stale through L1.
 *
 * The other difference from every other esp32 part is that the MMU is a
 * register port rather than a memory mapped table - see MMURead()/MMUWrite()
 * below.  Suspend and resume are ROM resident and therefore safe to call with
 * the cache down.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32p4SPIFlashCache.h"

#define ADDR2PAGE(addr)              ((addr) / kEsp32_RegisterMMU_PAGE_SIZE)
#define ADDR2OFF(addr)               ((addr) % kEsp32_RegisterMMU_PAGE_SIZE)
#define BYTES2PAGES(n)               (((n) + kEsp32_RegisterMMU_PAGE_SIZE - 1) / kEsp32_RegisterMMU_PAGE_SIZE)

/*******************************************************************************
 * ROM
*******************************************************************************/
/* Suspend returns the previous autoload setting, which resume takes back */
extern u32  Cache_Suspend_L2_Cache(void);
extern void Cache_Resume_L2_Cache(u32 autoload);
extern int  Cache_Invalidate_All(u32 map);

/* Cache_Invalidate_All() map bits, from rom/cache.h */
#define CACHE_MAP_L1_DCACHE          (0x01u << 4)
#define CACHE_MAP_L2_CACHE           (0x01u << 5)

/*
 * Plain u32, not LTAtomic.  Every caller brackets the suspend/resume pair with
 * Esp32DisableInterrupts(), so the count is never contended - and it must not be
 * atomic: this core is rv32imafc, with no A extension, so LTAtomic_* become
 * out-of-line calls into libatomic, which lives in flash.  Calling one from
 * EnableCache() below - with the cache still suspended - fetches instructions
 * that cannot be fetched.  The esp32s3 file beside this one can use LTAtomic
 * because Xtensa inlines it.
 */
static u32 s_cacheRefCount = 0;

/****************************************************************************
 * Flash encryption
 *
 * Duplicated from Esp32p4SPIFlash_IsEncryptionEnabled() rather than called,
 * because that one lives in flash and every caller here has the cache
 * suspended.  The parity is folded by hand for the same reason -
 * __builtin_parity() on a 3 bit field becomes an out-of-line libgcc call.
 ****************************************************************************/
static bool ESP32_MEM_REGION(IRAM) EncryptionEnabled(void) {
    u32 nCryptCount = (ESP32_REG(EFUSE_RD_REPEAT_DATA1) & ESP32_REG_MASK(EFUSE, SPI_BOOT_CRYPT_CNT))
                          >> ESP32_REG_SHIFT(EFUSE, SPI_BOOT_CRYPT_CNT);

    /* Three bits wide, an odd number of them set means encryption is on */
    return (((nCryptCount >> 2) ^ (nCryptCount >> 1) ^ nCryptCount) & 0x01) != 0;
}

/****************************************************************************
 * MMU access
 *
 * An entry is reached by writing its index to one register and then reading or
 * writing its content through another, so the pair is a port and the two
 * accesses must not be separated.  Every caller here already holds interrupts
 * off with the cache suspended, which is what keeps that true.
 ****************************************************************************/
static u32 ESP32_MEM_REGION(IRAM) MMURead(u32 nIndex) {
    ESP32_REG(MMU_ITEM_INDEX) = nIndex;
    return ESP32_REG(MMU_ITEM_CONTENT);
}

static void ESP32_MEM_REGION(IRAM) MMUWrite(u32 nIndex, u32 nContent) {
    ESP32_REG(MMU_ITEM_INDEX)   = nIndex;
    ESP32_REG(MMU_ITEM_CONTENT) = nContent;
}

/****************************************************************************
 * Flush the flash cache
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32p4SPIFlashCache_Flush(void) {
    /* The cache is read only over flash, so invalidating is all that is needed
     * to make new MMU entries visible; there is nothing to write back.  Both
     * levels, because the L1 data cache sits in front of the L2 that backs
     * flash and would otherwise keep serving the old mapping. */
    Cache_Invalidate_All(CACHE_MAP_L1_DCACHE | CACHE_MAP_L2_CACHE);
}

/****************************************************************************
 * Map SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32p4SPIFlashCache_Mmap(Esp32p4SPIFlash_MapInfo * pInfo) {
    pInfo->ptr          = NULL;
    pInfo->startPage    = 0;
    pInfo->pageCount    = 0;

    /* One table serves both buses on this part, and irom and drom are already
     * mapped by the time this runs, so an entry that reads back invalid is
     * genuinely free whichever segment it neighbours. */
    u32 startPage = 0;
    for (startPage = 0; startPage < kEsp32_RegisterMMU_ENTRY_COUNT; ++startPage) {
        if (!(MMURead(startPage) & ESP32_REG_VAL(MMU, VALID))) {
            break;
        }
    }

    /* Reads through the cache come back as ciphertext unless the page is marked
     * sensitive, so once encryption is burned in every mapping needs the bit. */
    u32 nSensitive = EncryptionEnabled() ? ESP32_REG_VAL(MMU, SENSITIVE) : 0;

    u32 flashPage = ADDR2PAGE(pInfo->srcAddr);
    u32 pageCount = BYTES2PAGES(ADDR2OFF(pInfo->srcAddr) + pInfo->size);
    if (startPage + pageCount < kEsp32_RegisterMMU_ENTRY_COUNT) {
        for (u32 i = 0; i < pageCount; i++) {
            MMUWrite(startPage + i, ((flashPage + i) & ESP32_REG_MASK(MMU, ADDRESS)) |
                                    ESP32_REG_VAL(MMU, VALID) |
                                    ESP32_REG_VAL(MMU, ACCESS_FLASH) |
                                    nSensitive);
        }

        pInfo->startPage = startPage;
        pInfo->pageCount = pageCount;
        pInfo->ptr = (void *)(kEsp32_RegisterMMU_FLASH_LOW + startPage * kEsp32_RegisterMMU_PAGE_SIZE +
                              ADDR2OFF(pInfo->srcAddr));
    }

    Esp32p4SPIFlashCache_Flush();
}

/****************************************************************************
 * Unmap SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32p4SPIFlashCache_Ummap(const Esp32p4SPIFlash_MapInfo * pInfo) {
    for (u32 i = pInfo->startPage; i < pInfo->startPage + pInfo->pageCount; ++i) {
        MMUWrite(i, ESP32_REG_VAL(MMU, INVALID));
    }

    Esp32p4SPIFlashCache_Flush();
}

/****************************************************************************
 * Disables the cache. Must run from IRAM
 ****************************************************************************/
u32 ESP32_MEM_REGION(IRAM) Esp32p4SPIFlashCache_DisableCache(void) {
    u32 state = 0;

    /* IRAM section begin - Code from here on must run from IRAM */

    if (s_cacheRefCount++ == 0) {
        state = Cache_Suspend_L2_Cache();
    }

    /* IRAM section continue - Code should keep running from IRAM until the cache is re-enabled */

    return state;
}

/****************************************************************************
 * Enables the cache. Must run from IRAM
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32p4SPIFlashCache_EnableCache(u32 state) {
    /* IRAM section begin - Code from here on must run from IRAM */
    if (s_cacheRefCount-- == 1) {
        Cache_Resume_L2_Cache(state);
    }
    /* IRAM section end - Code from here on can run from flash */
}

static u32 ESP32_IRAM_FUNC ParanoidMMURead(u32 nPageIn) {
    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32p4SPIFlashCache_DisableCache();

    u32 nPageOut = MMURead(nPageIn);

    Esp32p4SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return nPageOut;
}

bool Esp32p4SPIFlashCache_BusAddressToByteOffset(void * pAddress, u32 * pByteOffset) {
    u32 nAddress = (u32)pAddress;

    /* Instruction and data reach flash through the same window here, so unlike
     * the esp32c3 there is one range to test rather than two */
    if ((nAddress < kEsp32_RegisterMMU_FLASH_LOW) || (nAddress >= kEsp32_RegisterMMU_FLASH_HIGH)) {
        return false;
    }

    u32 nPage = (nAddress & kEsp32_RegisterMMU_VADDR_MASK) / kEsp32_RegisterMMU_PAGE_SIZE;
    if (nPage >= kEsp32_RegisterMMU_ENTRY_COUNT) return false;

    u32 nEntry = ParanoidMMURead(nPage);
    if (!(nEntry & ESP32_REG_VAL(MMU, VALID))) return false;

    *pByteOffset = ((nEntry & ESP32_REG_MASK(MMU, ADDRESS)) * kEsp32_RegisterMMU_PAGE_SIZE) |
                   (nAddress & (kEsp32_RegisterMMU_PAGE_SIZE - 1));
    return true;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  23-Sep-26   claudius    created, from esp32c3/Esp32c3SPIFlashCache.c
 *  29-Sep-26   claudius    mark mapped pages sensitive when flash encryption
 *                          is burned in
 */
