/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/esp32c3/Esp32c3SPIFlashCache.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * The esp32c3 has a single cache serving both buses, so there is one
 * suspend/resume pair here where the esp32s3 file beside this one has two, and
 * the state word the suspend returns is a bare autoload flag rather than a pair
 * packed into one u32.  The esp32s3's errata wrappers have no counterpart
 * either: its ROM suspend can return before the cache has gone idle, this one
 * cannot, so the ROM entry points are called directly.
 *
 * Suspend and resume are ROM resident and therefore safe to call with the cache
 * down.  One 128 entry MMU table is shared by both buses; see the Esp32_RegisterMMU
 * comment in include/esp32c3/Esp32_Registers.h for how the linker splits it.
 */

#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32c3SPIFlashCache.h"

#define ADDR2PAGE(addr)              ((addr) / kEsp32_RegisterMMU_PAGE_SIZE)
#define ADDR2OFF(addr)               ((addr) % kEsp32_RegisterMMU_PAGE_SIZE)
#define BYTES2PAGES(n)               (((n) + kEsp32_RegisterMMU_PAGE_SIZE - 1) / kEsp32_RegisterMMU_PAGE_SIZE)

/*******************************************************************************
 * ROM
*******************************************************************************/
/* Suspend returns the previous autoload setting, which resume takes back */
extern u32  Cache_Suspend_ICache(void);
extern void Cache_Resume_ICache(u32 autoload);
extern void Cache_Invalidate_ICache_All(void);

/*
 * Plain u32, not LTAtomic.  Every caller brackets the suspend/resume pair with
 * Esp32DisableInterrupts(), so the count is never contended - and it must not be
 * atomic: this core is rv32imc, with no A extension, so LTAtomic_* become
 * out-of-line calls into libatomic, which lives in flash.  Calling one from
 * EnableCache() below - with the cache still suspended - fetches instructions
 * that cannot be fetched.  The esp32s3 file beside this one can use LTAtomic
 * because Xtensa inlines it.
 */
static u32 s_cacheRefCount = 0;

/****************************************************************************
 * Flush the flash cache
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32c3SPIFlashCache_Flush(void) {
    /* The cache is read only over flash, so invalidating is all that is needed
     * to make new MMU entries visible; there is nothing to write back */
    Cache_Invalidate_ICache_All();
}

/****************************************************************************
 * Map SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32c3SPIFlashCache_Mmap(Esp32c3SPIFlash_MapInfo * pInfo) {
    volatile u32 * pMMUTable = ESP32_REG_ADDR(MMU_TABLE);

    pInfo->ptr          = NULL;
    pInfo->startPage    = 0;
    pInfo->pageCount    = 0;

    /* IROM holds the low entries and DROM sits above it - one table split by the
     * linker, see sections.ld - and both are already mapped by the time this
     * runs, so an entry that reads back invalid is genuinely free whichever
     * segment it neighbours. */
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

    Esp32c3SPIFlashCache_Flush();
}

/****************************************************************************
 * Unmap SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32c3SPIFlashCache_Ummap(const Esp32c3SPIFlash_MapInfo * pInfo) {
    volatile u32 * pMMUTable = ESP32_REG_ADDR(MMU_TABLE);
    for (u32 i = pInfo->startPage; i < pInfo->startPage + pInfo->pageCount; ++i) {
        pMMUTable[i] = ESP32_REG_VAL(MMU, INVALID);
    }

    Esp32c3SPIFlashCache_Flush();
}

/****************************************************************************
 * Disables the cache. Must run from IRAM
 ****************************************************************************/
u32 ESP32_MEM_REGION(IRAM) Esp32c3SPIFlashCache_DisableCache(void) {
    u32 state = 0;

    /* IRAM section begin - Code from here on must run from IRAM */

    if (s_cacheRefCount++ == 0) {
        state = Cache_Suspend_ICache();
    }

    /* IRAM section continue - Code should keep running from IRAM until the cache is re-enabled */

    return state;
}

/****************************************************************************
 * Enables the cache. Must run from IRAM
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32c3SPIFlashCache_EnableCache(u32 state) {
    /* IRAM section begin - Code from here on must run from IRAM */
    if (s_cacheRefCount-- == 1) {
        Cache_Resume_ICache(state);
    }
    /* IRAM section end - Code from here on can run from flash */
}

static u32 ESP32_IRAM_FUNC ParanoidMMURead(u32 nPageIn) {
    volatile u32 * pMMUTable = ESP32_REG_ADDR(MMU_TABLE);

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32c3SPIFlashCache_DisableCache();

    u32 nPageOut = pMMUTable[nPageIn];

    Esp32c3SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return nPageOut;
}

bool Esp32c3SPIFlashCache_BusAddressToByteOffset(void * pAddress, u32 * pByteOffset) {
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
    /* Flash is the only backing store on this part, so unlike the esp32s3 there
     * is no external RAM select bit to reject - only the invalid marker */
    if (nEntry & ESP32_REG_VAL(MMU, INVALID)) return false;

    *pByteOffset = ((nEntry & ESP32_REG_MASK(MMU, ADDRESS)) * kEsp32_RegisterMMU_PAGE_SIZE) |
                   (nAddress & (kEsp32_RegisterMMU_PAGE_SIZE - 1));
    return true;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  17-Sep-26   claudius    created
 */
