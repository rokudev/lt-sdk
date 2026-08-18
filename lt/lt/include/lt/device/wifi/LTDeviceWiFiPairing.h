/*******************************************************************************
 * <lt/device/wifi/LTDeviceWiFiPairing.h> LTDeviceWiFiPairing
 *
 * WPS Push-Button-Configuration (PBC) enrollee surface for WiFi pairing.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltdevice_wifi_pairing LTDeviceWiFiPairing
 * @ingroup ltdevice
 * @brief WPS PBC enrollee surface, kept separate from the STA data path.
 *
 * @internal
 *
 * The standard STA/AP link-layer surface lives in @ref LTDeviceWiFi.  WPS PBC
 * pairing is a distinct, out-of-band provisioning flow: the enrollee associates
 * (open system) to a Registrar advertising Push-Button-Configuration, runs the
 * WPS (EAP-WSC) exchange, and harvests a credential.  The actual connection
 * happens afterward via a normal configured join on @ref LTDeviceWiFi using the
 * harvested credential.
 *
 * Keeping this surface out of @ref LTDeviceWiFi means products that never pair
 * over WiFi do not link the WPS path, and the host-selection / vendor-IE policy
 * stays in the application/test layer (passed in via StartPbc()).
 *
 * LTDeviceWiFiPairing sits at Layer 5 over LTDriverWiFiPairing (Layer 6).
 * Callers must open LTDeviceWiFi and wait for kLTDeviceWiFi_Status_Up before
 * calling StartPbc().
 *
 * @endinternal
 */

#ifndef LT_INCLUDE_LT_DEVICE_WIFI_LTDEVICEWIFIPAIRING_H
#define LT_INCLUDE_LT_DEVICE_WIFI_LTDEVICEWIFIPAIRING_H

#include <lt/LTObject.h>
#include <lt/device/wifi/LTWiFi.h>

LT_EXTERN_C_BEGIN

/* _____________________________
   LTDeviceWiFiPairing API */
typedef_LTObject(LTDeviceWiFiPairing, 1) {
    /**< LTDeviceWiFiPairing object API.
     *
     *   @note To use LTDeviceWiFiPairing call lt_createobject, start a PBC
     *         session, poll for credentials, then destroy the object, e.g. <pre>
     *         LTDeviceWiFiPairing *pairing = lt_createobject(LTDeviceWiFiPairing);
     *         pairing->API->StartPbc(pairing, 120, &m1Ie, &assocIe, &target);
     *         if (pairing->API->GetCredentials(pairing, &creds)) { ... }
     *         lt_destroyobject(pairing);
     *         </pre>
     */

    bool (*StartPbc)(LTDeviceWiFiPairing *pairing, u32 timeoutSec,
                     LTWiFi_VendorIE const *wpsM1Ie,
                     LTWiFi_VendorIE const *assocVendorIE,
                     LTWiFi_ApInfo const *target);
        /**<
         * @brief Start a WPS PBC enrollee session.
         *
         * The supplicant performs an open-system association to a PBC Registrar
         * and runs the WPS (EAP-WSC) registration exchange.  On success a
         * credential becomes available via GetCredentials(); the caller then
         * performs a normal WPA2-PSK join through LTDeviceWiFi.
         *
         * Targeted vs. open PBC: host-selection policy (which AP is the intended
         * host) belongs to the application/test layer.  Scan via LTDeviceWiFi,
         * pick the host, then pass it here as @p target.  This library contains
         * no host-selection or vendor-OUI policy.
         *
         * @param[in] pairing: this object.
         * @param[in] timeoutSec: walk time / how long PBC stays active.
         * @param[in] wpsM1Ie: optional vendor-specific extension embedded in the
         *            WPS M1 message (e.g. the Roku pairing IE: OUI + msg-type +
         *            chip id).  Pass NULL for a plain WPS PBC session.  The IE
         *            payload is copied; the caller need not keep it alive.
         * @param[in] assocVendorIE: optional vendor-specific IE appended to the
         *            WPS (re)association request, after the standard WPS IE.
         *            Some registrars (e.g. the Roku host) require a product
         *            vendor IE in the assoc request to accept a credential-less
         *            enrollee.  Pass NULL for none.  The IE payload is copied.
         * @param[in] target: optional AP to direct the PBC walk at.  When set,
         *            the BSSID (and, when known, channel) steer the supplicant to
         *            a specific AP rather than any AP in PBC mode.  Pass NULL to
         *            let the supplicant pick any PBC AP.  Only bssid / channel /
         *            ssid fields are consumed; the descriptor need not persist.
         * @return true if PBC mode started.
         */

    void (*Stop)(LTDeviceWiFiPairing *pairing);
        /**<
         * @brief Stop an active WPS PBC session (no-op if not active).
         *
         * @param[in] pairing: this object.
         */

    bool (*GetCredentials)(LTDeviceWiFiPairing *pairing, LTWiFi_Credentials *out);
        /**<
         * @brief Return credentials captured by the last successful WPS PBC.
         *
         * @param[in]  pairing: this object.
         * @param[out] out: receives ssid / passphrase / security.
         * @return true if credentials are available.
         */
} LTOBJECT_API;

LT_EXTERN_C_END

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_WIFI_LTDEVICEWIFIPAIRING_H */
