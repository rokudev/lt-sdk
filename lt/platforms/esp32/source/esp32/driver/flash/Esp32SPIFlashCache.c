/*******************************************************************************
 * platforms/esp32/source/esp32/driver/flash/Esp32SPIFlashCache.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * Modified by Roku, Inc. Please see changelog below for more information.
 *
 ****************************************************************************/
#include <lt/LTTypes.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTCore.h>

#include "Esp32_SoC.h"
#include "Esp32_Registers.h"
#include "Esp32_Irq.h"
#include "Esp32SPIFlashCache.h"

#define MMU_PAGE_SIZE                0x10000
#define MMU_ADDR_MASK                0xff

#define ADDR2PAGE(addr)              ((addr) / MMU_PAGE_SIZE)
#define ADDR2OFF(addr)               ((addr) % MMU_PAGE_SIZE)
#define BYTES2PAGES(n)               (((n) + MMU_PAGE_SIZE - 1) / MMU_PAGE_SIZE)

#define VADDR0_START_ADDR            0x3f400000
#define VADDR1_START_ADDR            0x40000000
#define VADDR1_FIRST_USABLE_ADDR     0x400d0000

#define DROM0_PAGES_START            0
#define DROM0_NUM_PAGES              64
#define IROM0_PAGES_START            64
#define IROM0_PAGES_END              256
#define MMU_PAGES_LIMIT              256

static LTAtomic s_cacheRefCount    = { 0 };

/****************************************************************************
 * Flush the flash cache
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32SPIFlashCache_Flush(void) {
    u32 regval  = ESP32_REG(DPORT_PRO_CACHE_CTRL);

    regval &= ~ESP32_REG_MASK(DPORT_CACHE_CTRL, FLUSH_ENA);
    ESP32_REG(DPORT_PRO_CACHE_CTRL) = regval;
    regval |= ESP32_REG_MASK(DPORT_CACHE_CTRL, FLUSH_ENA);
    ESP32_REG(DPORT_PRO_CACHE_CTRL) = regval;

    // wait for the flush to complete
    u16 timeout = LT_U16_MAX;
    while ((ESP32_REG(DPORT_PRO_CACHE_CTRL) & ESP32_REG_MASK(DPORT_CACHE_CTRL, FLUSH_DONE)) == 0 && --timeout != 0);
}

/****************************************************************************
 * Map SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32SPIFlashCache_Mmap(Esp32SPIFlash_MapInfo * pInfo) {
    volatile u32 * pProCacheMMUTable = &ESP32_REG(DPORT_CACHE_MMU_TABLE);

    pInfo->ptr          = NULL;
    pInfo->startPage    = 0;
    pInfo->pageCount    = 0;

    u32 startPage = 0;
    for (startPage = 0; startPage < DROM0_NUM_PAGES; ++startPage) {
        if (pProCacheMMUTable[startPage] == ESP32_REG_VAL(DPORT_CACHE_MMU_TABLE, INVALID)) {
            break;
        }
    }

    u32 flashPage = ADDR2PAGE(pInfo->srcAddr);
    u32 pageCount = BYTES2PAGES(ADDR2OFF(pInfo->srcAddr) + pInfo->size);
    if ((startPage + pageCount) < DROM0_NUM_PAGES) {
        if (startPage + pageCount < DROM0_NUM_PAGES) {
            for (u32 i = 0; i < pageCount; i++) {
                pProCacheMMUTable[startPage + i] = flashPage + i;
            }

            pInfo->startPage = startPage;
            pInfo->pageCount = pageCount;
            pInfo->ptr = (void *)(VADDR0_START_ADDR + startPage * MMU_PAGE_SIZE +
                                ADDR2OFF(pInfo->srcAddr));
        }
    }

    Esp32SPIFlashCache_Flush();
}

/****************************************************************************
 * Unmap SPI Flash address
 * @note the cache must be disabled before calling this function
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32SPIFlashCache_Ummap(const Esp32SPIFlash_MapInfo *pInfo) {
    volatile u32 * pProCacheMMUTable = &ESP32_REG(DPORT_CACHE_MMU_TABLE);
    for (u32 i = pInfo->startPage; i < pInfo->startPage + pInfo->pageCount; ++i) {
        pProCacheMMUTable[i] = ESP32_REG_VAL(DPORT_CACHE_MMU_TABLE, INVALID);
    }

    Esp32SPIFlashCache_Flush();
}

/****************************************************************************
 * Disables the cache. Must run from IRAM
 ****************************************************************************/
u32 ESP32_MEM_REGION(IRAM) Esp32SPIFlashCache_DisableCache(void) {
    u32 state = ESP32_REG(DPORT_PRO_CACHE_CTRL1) & ESP32_REG_MASK(DPORT_CACHE_CTRL1, CACHE);
    while (((ESP32_REG(DPORT_PRO_DCACHE_DBUG0) >> ESP32_REG_SHIFT(DPORT_DCACHE_DBUG0, STATE)) & ESP32_REG_MASK(DPORT_DCACHE_DBUG0, STATE)) != 1);

    LTAtomic_FetchAdd(&s_cacheRefCount, 1);

    /* IRAM section begin - Code from here on must run from IRAM */

    ESP32_REG(DPORT_PRO_CACHE_CTRL) &= ~ESP32_REG_MASK(DPORT_CACHE_CTRL, ENABLE);

    /* IRAM section continue - Code should keep running from IRAM until the cache is re-enabled */

    return state;
}

/****************************************************************************
 * Enables the cache. Must run from IRAM
 ****************************************************************************/
void ESP32_MEM_REGION(IRAM) Esp32SPIFlashCache_EnableCache(u32 state) {
    /* IRAM section begin - Code from here on must run from IRAM */
    if (LTAtomic_FetchSubtract(&s_cacheRefCount, 1) == 1) {
        ESP32_REG(DPORT_PRO_CACHE_CTRL)  |= ESP32_REG_MASK(DPORT_CACHE_CTRL, ENABLE);
        ESP32_REG(DPORT_PRO_CACHE_CTRL1) |= ESP32_REG_MASK(DPORT_CACHE_CTRL1, CACHE) & state;
    }
    /* IRAM section end - Code from here on can run from flash */
}

static u32 ESP32_MEM_REGION(IRAM) LT_NOINLINE ParanoidMMURead(u32 nPageIn) {
    volatile u32 * pProCacheMMUTable = &ESP32_REG(DPORT_CACHE_MMU_TABLE);

    u32 mask = Esp32DisableInterrupts();

    /* IRAM section begin - Code from here on must run from IRAM */

    u32 state = Esp32SPIFlashCache_DisableCache();

    u32 nPageOut = pProCacheMMUTable[nPageIn];

    Esp32SPIFlashCache_EnableCache(state);

    /* IRAM section end - Code from here on can run from flash */

    Esp32EnableInterrupts(mask);

    return nPageOut;
}

bool Esp32SPIFlashCache_BusAddressToByteOffset(void * pAddress, u32 * pByteOffset) {
    u32 nAddress = (u32)pAddress;
    u32 nPage;
    if (nAddress >= VADDR1_FIRST_USABLE_ADDR) {
        nPage = (nAddress - VADDR1_START_ADDR) / MMU_PAGE_SIZE + IROM0_PAGES_START;
    } else if (nAddress < VADDR1_START_ADDR) {
        nPage = (nAddress - VADDR0_START_ADDR) / MMU_PAGE_SIZE + DROM0_PAGES_START;
    } else {
        return false;
    }
    if (nPage > MMU_PAGES_LIMIT) return false;
    nPage = ParanoidMMURead(nPage);
    if (nPage == ESP32_REG_VAL(DPORT_CACHE_MMU_TABLE, INVALID)) return false;
    *pByteOffset = ((nPage & MMU_ADDR_MASK) * MMU_PAGE_SIZE) | (nAddress & (MMU_PAGE_SIZE - 1));
    return true;
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  29-Jun-22   vitellius   created
 *  29-Jun-22   vitellius   mmap and ummap functions were modified by Roku LT
 */
