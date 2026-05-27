/*******************************************************************************
 * <lt/driver/sdio/LTDriverSdio.h> LTDriverSdio
 *
 * Interface for an SDIO driver
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LT_INCLUDE_LT_DRIVER_SDIO_LTDRIVERSDIO_H
#define LT_INCLUDE_LT_DRIVER_SDIO_LTDRIVERSDIO_H

#include <lt/LTTypes.h>
#include <lt/device/sdio/LTDeviceSdio.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverSdio, 1) {
    void (*EnumerateCardIds)(LTDeviceSdio_EnumerateCardIdsProc *pCardIdProc, void *pClientData);
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DRIVER_SDIO_LTDRIVERSDIO_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Jun-24   commodus    created
 */

