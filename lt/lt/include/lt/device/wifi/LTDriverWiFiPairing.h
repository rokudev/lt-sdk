/*******************************************************************************
 * <lt/device/wifi/LTDriverWiFiPairing.h> LTDriverWiFiPairing
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef LTDRIVERWIFIPAIRING_H
#define LTDRIVERWIFIPAIRING_H

#include <lt/LTTypes.h>
#include <lt/device/wifi/LTWiFi.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_INTERFACE(LTDriverWiFiPairing, 1) {
    bool (*StartWpsPbc)(u32 timeoutSec, LTWiFi_VendorIE const *wpsM1Ie,
                        LTWiFi_VendorIE const *assocVendorIE, LTWiFi_ApInfo const *target);
    /**<
     * @brief Start WPS PBC enrollee mode.
     *
     * @param[in] timeoutSec: walk time / how long to keep PBC active. Note: the BL61x
     *   driver does not wire this into the WPS registrar timeout; the PBC window is
     *   governed entirely by the WPS registrar / supplicant (fixed at 30 s). Pass 0 if
     *   the value is not meaningful for the target platform.
     * @param[in] wpsM1Ie: optional vendor extension embedded in the WPS M1 message.
     * @param[in] assocVendorIE: optional vendor IE appended to WPS association requests.
     * @param[in] target: AP selected by the caller's host-selection policy.
     * @return TRUE if PBC mode started, FALSE otherwise
     */

    void (*StopWpsPbc)(void);
    /**<
     * @brief Stop a previously started WPS PBC session.
     */

    bool (*GetWpsCredentials)(LTWiFi_Credentials *out);
    /**<
     * @brief Retrieve credentials captured by the last WPS PBC session.
     *
     * @param[out] out: receives ssid / passphrase / security on success
     * @return TRUE when credentials are valid
     */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* LTDRIVERWIFIPAIRING_H */
