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

/* LTBootDriver API callable from internal bootloader functions */

#include "esp_efuse.h"
#include "esp_efuse_table.h"

#include "LTBootDriver.h"

LTBootSecurityCheck CheckEFuseBitIsSet(const esp_efuse_desc_t * pField[]);
LTBootSecurityCheck CheckEFuseBitIsClear(const esp_efuse_desc_t * pField[]);

#define IsSecureBootEnabled()  (CheckEFuseBitIsSet(ESP_EFUSE_ABS_DONE_1) == kLTBootSecurityCheck_Pass)
#define IsSecureBootDisabled() (CheckEFuseBitIsClear(ESP_EFUSE_ABS_DONE_1) == kLTBootSecurityCheck_Pass)

LTBootSecurityCheck GetApplicationSecureBootKeyDigest(const void ** ppDigest);

#endif

