/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32s3/Esp32s3SPIFlashCache.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32s3 cache is not a register level port of the esp32 one.  Where the
 * esp32 had a single cache per core, controlled entirely from DPORT, and one MMU
 * page table per bus, the esp32s3 has a separate instruction and data cache,
 * each with its own suspend/resume and autoload state, and one 512 entry MMU
 * page table shared by both buses.
 *
 * Suspend/resume lives in ROM, so it is safe to call with the cache down; this
 * file uses the ROM entry points rather than driving EXTMEM directly.  The
 * un-prefixed Cache_Suspend_ICache/Cache_Suspend_DCache names are NOT the ROM
 * ones - on the esp32s3 the ROM versions can return before the cache has gone
 * idle, so IDF renames them rom_Cache_Suspend_* and wraps them.  The wrappers
 * below are that same errata workaround.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32s3SPIFlashCache.h"

#define ADDR2PAGE(addr)              ((addr) / kEsp32_RegisterMMU_PAGE_SIZE)
#define ADDR2OFF(addr)               ((addr) % kEsp32_RegisterMMU_PAGE_SIZE)
#define BYTES2PAGES(n)               (((n) + kEsp32_RegisterMMU_PAGE_SIZE - 1) / kEsp32_RegisterMMU_PAGE_SIZE)

/*******************************************************************************
 * ROM
*******************************************************************************/
/* Suspend returns the previous autoload setting, which resume takes back */
extern u32  rom_Cache_Suspend_ICache(void);
extern u32  rom_Cache_Suspend_DCache(void);
extern void Cache_Resume_ICache(u32 autoload);
extern void Cache_Resume_DCache(u32 autoload);
extern void Cache_Invalidate_DCache_All(void);
extern void Cache_Invalidate_ICache_All(void);

static LTAtomic s_cacheRefCount    = { 0 };

/****************************************************************************
 * Spin until the named cache reports idle - see the errata note above
 ****************************************************************************/
static void ESP32_IRAM_FUNC WaitCacheIdle(bool bICache) {
    if (bICache) {
        while (((ESP32_REG(EXTMEM_CACHE_STATE) & ESP32_REG_MASK(EXTMEM_CACHE_STATE, ICACHE))
                    >> ESP32_REG_SHIFT(EXTMEM_CACHE_STATE, ICACHE)) != ESP32_REG_VAL(EXTMEM_CACHE_STATE, IDLE));
    } else {
        while (((ESP32_REG(EXTMEM_CACHE_STATE) & ESP32_REG_MASK(EXTMEM_CACHE_STATE, DCACHE))
                    >> ESP32_REG_SHIFT(EXTMEM_CACHE_STATE, DCACHE)) != ESP32_REG_VAL(EXTMEM_CACHE_STATE, IDLE));
    }
}

/****************************************************************************
 * Flush the flash cache
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32s3SPIFlashCache_Flush(void) {
    /* Both caches are read only over flash, so invalidating is all that is
     * needed to make new MMU entries visible; there is nothing to write back */
    Cache_Invalidate_DCache_All();
    Cache_Invalidate_ICache_All();
}

/****************************************************************************
 * Map SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32s3SPIFlashCache_Mmap(Esp32s3SPIFlash_MapInfo * pInfo) {
    volatile u32 * pMMUTable = ESP32_REG_ADDR(MMU_TABLE);

    pInfo->ptr          = NULL;
    pInfo->startPage    = 0;
    pInfo->pageCount    = 0;

    /* Entries below DROM_MAX_END are the flash half of the table; above it is
     * PSRAM's.  Within that half IROM holds the low entries and DROM sits above
     * it - one shared table, split by the linker, see sections.ld - and both are
     * already mapped by the time this runs, so an entry that reads back invalid
     * is genuinely free whichever segment it neighbours. */
    u32 startPage = 0;
    for (startPage = 0; startPage < kEsp32_RegisterMMU_DROM_MAX_END; ++startPage) {
        if (pMMUTable[startPage] == ESP32_REG_VAL(MMU, INVALID)) {
            break;
        }
    }

    u32 flashPage = ADDR2PAGE(pInfo->srcAddr);
    u32 pageCount = BYTES2PAGES(ADDR2OFF(pInfo->srcAddr) + pInfo->size);
    if (startPage + pageCount < kEsp32_RegisterMMU_DROM_MAX_END) {
        for (u32 i = 0; i < pageCount; i++) {
            pMMUTable[startPage + i] = flashPage + i;
        }

        pInfo->startPage = startPage;
        pInfo->pageCount = pageCount;
        pInfo->ptr = (void *)(kEsp32_RegisterMMU_DBUS_LOW + startPage * kEsp32_RegisterMMU_PAGE_SIZE +
                              ADDR2OFF(pInfo->srcAddr));
    }

    Esp32s3SPIFlashCache_Flush();
}

/****************************************************************************
 * Unmap SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32s3SPIFlashCache_Ummap(const Esp32s3SPIFlash_MapInfo * pInfo) {
    volatile u32 * pMMUTable = ESP32_REG_ADDR(MMU_TABLE);
    for (u32 i = pInfo->startPage; i < pInfo->startPage + pInfo->pageCount; ++i) {
        pMMUTable[i] = ESP32_REG_VAL(MMU, INVALID);
    }

    Esp32s3SPIFlashCache_Flush();
}

/****************************************************************************
 * Disables the cache. Must run from IRAM
 ****************************************************************************/
u32 ESP32_MEM_REGION(IRAM) Esp32s3SPIFlashCache_DisableCache(void) {
    u32 state = 0;

    /* IRAM section begin - Code from here on must run from IRAM */

    if (LTAtomic_FetchAdd(&s_cacheRefCount, 1) == 0) {
        u32 nICacheAutoload = rom_Cache_Suspend_ICache();
        WaitCacheIdle(true);
        u32 nDCacheAutoload = rom_Cache_Suspend_DCache();
        WaitCacheIdle(false);

        state = (nICacheAutoload << 16) | (nDCacheAutoload & 0xffff);
    }

    /* IRAM section continue - Code should keep running from IRAM until the cache is re-enabled */

    return state;
}

/****************************************************************************
 * Enables the cache. Must run from IRAM
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32s3SPIFlashCache_EnableCache(u32 state) {
    /* IRAM section begin - Code from here on must run from IRAM */
    if (LTAtomic_FetchSubtract(&s_cacheRefCount, 1) == 1) {
        /* Resume in the reverse order of the suspend above */
        Cache_Resume_DCache(state & 0xffff);
        Cache_Resume_ICache(state >> 16);
    }
    /* IRAM section end - Code from here on can run from flash */
}

static u32 ESP32_IRAM_FUNC ParanoidMMURead(u32 nPageIn) {
    volatile u32 * pMMUTable = ESP32_REG_ADDR(MMU_TABLE);

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32s3SPIFlashCache_DisableCache();

    u32 nPageOut = pMMUTable[nPageIn];

    Esp32s3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return nPageOut;
}

bool Esp32s3SPIFlashCache_BusAddressToByteOffset(void * pAddress, u32 * pByteOffset) {
    u32 nAddress = (u32)pAddress;

    /* The MMU table is shared by both buses, and a page's index within it is
     * just its offset into whichever cached window reaches it */
    if (   (nAddress <  kEsp32_RegisterMMU_DBUS_LOW)
        || (nAddress >= kEsp32_RegisterMMU_IBUS_HIGH)
        || (nAddress >= kEsp32_RegisterMMU_DBUS_HIGH && nAddress < kEsp32_RegisterMMU_IBUS_LOW)) {
        return false;
    }

    u32 nPage = (nAddress & kEsp32_RegisterMMU_BUS_ADDR_MASK) / kEsp32_RegisterMMU_PAGE_SIZE;
    if (nPage >= kEsp32_RegisterMMU_ENTRY_COUNT) return false;

    u32 nEntry = ParanoidMMURead(nPage);
    /* Reject unmapped pages, and pages backed by external RAM rather than flash */
    if (nEntry & (ESP32_REG_VAL(MMU, INVALID) | ESP32_REG_MASK(MMU, ACCESS_SPIRAM))) return false;

    *pByteOffset = ((nEntry & ESP32_REG_MASK(MMU, ADDRESS)) * kEsp32_RegisterMMU_PAGE_SIZE) |
                   (nAddress & (kEsp32_RegisterMMU_PAGE_SIZE - 1));
    return true;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jul-26   claudius    created
 *  03-Aug-26   claudius    corrected the description of the irom/drom split
 *  05-Aug-26   claudius    ESP32_IRAM_FUNC
 */
