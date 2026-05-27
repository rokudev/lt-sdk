/*******************************************************************************
 * <lt/net/dtls/LTNetDtls.h> - Datagram Transport Layer Security (DTLS) Protocol
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_DTLS_LTNETDTLS_H
#define ROKU_LT_INCLUDE_LT_NET_DTLS_LTNETDTLS_H

#include <lt/LTTypes.h>
#include <lt/net/core/LTNetCore.h>
LT_EXTERN_C_BEGIN

/* Supported SRTP mode needs a maximum of :
 * - 16 bytes for key (AES-128)
 * - 14 bytes SALT
 * One for sender, one for receiver context
 */
#define MBEDTLS_TLS_SRTP_MAX_KEY_MATERIAL_LENGTH    60

// https://www.iana.org/assignments/srtp-protection/srtp-protection.xhtml#srtp-protection-1
typedef enum {
    kLTDtlsSrtpProtectionProfile_None                                     = 0,
    kLTDtlsSrtpProtectionProfile_AES128_CM_SHA1_80                        = 0x0001,
    kLTDtlsSrtpProtectionProfile_AES128_CM_SHA1_32                        = 0x0002,
    kLTDtlsSrtpProtectionProfile_NULL_HMAC_SHA1_80                        = 0x0005,
    kLTDtlsSrtpProtectionProfile_NULL_HMAC_SHA1_32                        = 0x0006,
    kLTDtlsSrtpProtectionProfile_AEAD_AES_128_GCM                         = 0x0007,
    kLTDtlsSrtpProtectionProfile_AEAD_AES_256_GCM                         = 0x0008,
    kLTDtlsSrtpProtectionProfile_DOUBLE_AEAD_AES_128_GCM_AEAD_AES_128_GCM = 0x0009,
    kLTDtlsSrtpProtectionProfile_DOUBLE_AEAD_AES_256_GCM_AEAD_AES_256_GCM = 0x000A,
    kLTDtlsSrtpProtectionProfile_SRTP_ARIA_128_CTR_HMAC_SHA1_80           = 0x000B,
    kLTDtlsSrtpProtectionProfile_SRTP_ARIA_128_CTR_HMAC_SHA1_32           = 0x000C,
    kLTDtlsSrtpProtectionProfile_SRTP_ARIA_256_CTR_HMAC_SHA1_80           = 0x000D,
    kLTDtlsSrtpProtectionProfile_SRTP_ARIA_256_CTR_HMAC_SHA1_32           = 0x000E,
    kLTDtlsSrtpProtectionProfile_SRTP_AEAD_ARIA_128_GCM                   = 0x000F,
    kLTDtlsSrtpProtectionProfile_SRTP_AEAD_ARIA_256_GCM                   = 0x0010,
} LTDtlsSrtpProtectionProfileId;

/*  _________________________
    Library Root Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTNetDtls, 1) {
/** ___________________________________
 *   @section LTNetDtls Library
 *   * @brief a library for ...
 *   *
 *   * LTNetDtls provides a simple interface for ...
 \______________________________________________________________________________________*/

    // TODO - remove this temporary hack when we move to using certs from LTSystemCrypto
    void (*GetFingerprint)(u8 fingerprint[32]);

    LTSocket (*WrapSocket)(LTSocket hTransportSocket,
                           const char *socketSpec);
        /**< @brief Create a new DTLS socket which wraps a transport socket.
         *
         *  The socketSpec parameter used to open the transport socket can include DTLS-related options,
         *  which this library will apply to the created DTLS socket. The supported options are:
         *     - udp                         - Inform network stack to open a udp socket for DTLS
         *     - cakeys:[CA 1 id],[CA 2 id],...,[CA N id] - A comma-separated list of CA root key ids
         *                                                  to be used to verify the server's certificate.
         *                                                  The actual key data for each CA key id will be
         *                                                  read from the keystore.
         *     - clientcert:[client cert id] - The identifier for the client certificate.
         *                                     The actual cert data will be read from the keystore.
         *
         *   The returned LTSocket interface supports the following properties:
         *     connect.endpoint.v4      [set] - the endpoint that will
         *     peer.fingerprint.sha256  [set] - the expected SHA256 fingerprint of the peer's certificate
         *     srtp.protectionprofile   [get] - the selected SRTP protection profile
         *     srtp.keymaterial         [get] - the exported SRTP key material
         *
         * @param hTransportSocket     The transport socket that will be used to carry TLS data
         * @param socketSpec           The socket spec used to create the transport socket
         */
}
LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_NET_DTLS_LTNETDTLS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-May-22   trajan      created
 *  18-Oct-22   augustus    updated for thread and event api enhancements
 *  13-Aug-23   valerian    updated
 */
