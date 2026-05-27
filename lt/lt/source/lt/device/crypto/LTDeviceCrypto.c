/******************************************************************************
 * lt/device/crypto/LTDeviceCrypto.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/LT.h>

// DEFINE_LTLOG_SECTION("dev.crypto");

/*******************************************************************************
 * Defined but nothing is needed from LTDeviceCrypto. */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDeviceCrypto, 1) LTLIBRARY_EMPTY_INTERFACE;

/*******************************************************
 * LTDeviceCrypto device library prototype functions */
static bool LTDeviceCryptoImpl_LibInit(void) {
    return true;
}

static void LTDeviceCryptoImpl_LibFini(void) {
}

define_LTLIBRARY_ROOT_INTERFACE(LTDeviceCrypto) {
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Jan-22   gallienus   created
 *  03-Apr-23   augustus    load driver from LTDeviceConfig specification
 *  05-May-25   gallienus   refactor to objects.
 */
