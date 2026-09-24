/*******************************************************************************
 * LTBootInternal.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef _LTBOOT_INTERNAL_H_
#define _LTBOOT_INTERNAL_H_

/* Angle brackets, not quotes: a quoted include searches this file's own directory
 * first, which would find the chip esp_efuse.h beside it - that one declares only
 * esp_efuse_block_t.  esp_efuse_desc_t comes from the generic header one level up,
 * which in turn includes the chip one. */
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include "LTBootDriver.h"

LTBootSecurityCheck CheckEFuseBitIsSet(const esp_efuse_desc_t * pField[]);
LTBootSecurityCheck CheckEFuseBitIsClear(const esp_efuse_desc_t * pField[]);

#define ESP_EFUSE_LT_SECURE_BOOT       ESP_EFUSE_SECURE_BOOT_EN

#define IsSecureBootEnabled()  (CheckEFuseBitIsSet(ESP_EFUSE_LT_SECURE_BOOT) == kLTBootSecurityCheck_Pass)
#define IsSecureBootDisabled() (CheckEFuseBitIsClear(ESP_EFUSE_LT_SECURE_BOOT) == kLTBootSecurityCheck_Pass)

LTBootSecurityCheck GetApplicationSecureBootKeyDigest(const void ** ppDigest);
#endif
