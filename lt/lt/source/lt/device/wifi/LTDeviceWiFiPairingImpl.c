/*******************************************************************************
 * lt/source/lt/device/wifi/LTDeviceWiFiPairingImpl.c
 *
 * WPS PBC enrollee surface.  Thin pass-through to LTDriverWiFiPairing, kept
 * separate from LTDeviceWiFi's STA data-path API.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDeviceWiFi.h>
#include <lt/device/wifi/LTDeviceWiFiPairing.h>
#include <lt/device/wifi/LTDriverWiFiPairing.h>
#include <lt/device/config/LTDeviceConfig.h>

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*_________________________________________________
  typedef_LTObjectImpl with private data members */
typedef_LTObjectImpl(LTDeviceWiFiPairing, LTDeviceWiFiPairingImpl) {
    LTDriverLibrary     *pDriver;
    LTDriverWiFiPairing *pPairing;
} LTOBJECT_API;

/*___________________________________________
  LTDeviceWiFiPairingImpl construct/destruct */
static bool LTDeviceWiFiPairingImpl_ConstructObject(LTDeviceWiFiPairingImpl *pairing) {
    LTCore *pCore = LT_GetCore();
    do {
        if (!(pairing->pDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceWiFiPairing", 0))) break;
        if (!(pairing->pPairing = (LTDriverWiFiPairing *)pCore->GetLibraryInterface(
                (LTLibrary *)pairing->pDriver, "LTDriverWiFiPairing"))) break;
        return true;
    } while (0);
    if (pairing->pDriver) { pCore->CloseLibrary((LTLibrary *)pairing->pDriver); pairing->pDriver = NULL; }
    pairing->pPairing = NULL;
    return false;
}

static void LTDeviceWiFiPairingImpl_DestructObject(LTDeviceWiFiPairingImpl *pairing) {
    LTCore *pCore = LT_GetCore();
    if (pairing->pDriver) { pCore->CloseLibrary((LTLibrary *)pairing->pDriver); pairing->pDriver = NULL; }
    pairing->pPairing = NULL;
}

/*_______________________________________
  LTDeviceWiFiPairingImpl API functions */
static bool LTDeviceWiFiPairingImpl_StartPbc(LTDeviceWiFiPairingImpl *pairing, u32 timeoutSec,
                                              LTWiFi_VendorIE const *wpsM1Ie,
                                              LTWiFi_VendorIE const *assocVendorIE,
                                              LTWiFi_ApInfo const *target) {
    if (!pairing->pPairing || !pairing->pPairing->StartWpsPbc) return false;
    return pairing->pPairing->StartWpsPbc(timeoutSec, wpsM1Ie, assocVendorIE, target);
}

static void LTDeviceWiFiPairingImpl_Stop(LTDeviceWiFiPairingImpl *pairing) {
    if (pairing->pPairing && pairing->pPairing->StopWpsPbc) pairing->pPairing->StopWpsPbc();
}

static bool LTDeviceWiFiPairingImpl_GetCredentials(LTDeviceWiFiPairingImpl *pairing,
                                                    LTWiFi_Credentials *out) {
    if (!pairing->pPairing || !pairing->pPairing->GetWpsCredentials || !out) return false;
    return pairing->pPairing->GetWpsCredentials(out);
}

/*________________________________
  LTDeviceWiFiPairing api binding */
define_LTObjectImplPublic(LTDeviceWiFiPairing, LTDeviceWiFiPairingImpl,
    StartPbc,
    Stop,
    GetCredentials
);
