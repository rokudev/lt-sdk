/******************************************************************************
 * FlashDevice.c                                 Generic Flash Device Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>

#include "lt/LTTypes.h"
#include "lt/core/LTCore.h"

#include "FlashDevice.h"

enum { kFlashDeviceRegistryMaxEntries = 64 };
static const FlashDeviceInterfaceMapEntry * s_flashDeviceRegistry[kFlashDeviceRegistryMaxEntries];
static size_t s_flashDeviceRegistryCount = 0;

static inline bool IsSet(const char * s) { return s && *s; }

static inline bool IsEqual(const char * a, const char * b) { return a && b && (lt_strcmp(a, b) == 0); }

bool FlashDevice_Register(const FlashDeviceInterfaceMapEntry * pEntry) {
    if (!pEntry || !pEntry->pInterface) return false;

    for (size_t i = 0; i < s_flashDeviceRegistryCount; i++) {
        if (s_flashDeviceRegistry[i] == pEntry) return true;
    }

    if (s_flashDeviceRegistryCount >= kFlashDeviceRegistryMaxEntries) return false;
    s_flashDeviceRegistry[s_flashDeviceRegistryCount++] = pEntry;
    return true;
}
// Obtain requested flash device interface
const FlashDeviceInterface * GetFlashDeviceInterface(const char * pDevice, const char * pFamily) {
    const bool searchFamily = IsSet(pFamily);

    for (size_t i = 0; i < s_flashDeviceRegistryCount; i++) {
        const FlashDeviceInterfaceMapEntry * e = s_flashDeviceRegistry[i];

        if (searchFamily) {
            // Must have matching family
            if (!IsEqual(pFamily, e->pFamily)) continue;

            // If entry is refined by device, that must match too
            if (IsSet(e->pDevice) && !IsEqual(pDevice, e->pDevice)) continue;

            return e->pInterface;
        } else {
            // Legacy lookup: match either legacy alias or primary device name
            if (IsEqual(pDevice, e->pLegacyDevice) || IsEqual(pDevice, e->pDevice)) {
                return e->pInterface;
            }
        }
    }

    if (searchFamily) {
        printf("Unknown device family: %s\n", pFamily);
    }
    return NULL;
}
