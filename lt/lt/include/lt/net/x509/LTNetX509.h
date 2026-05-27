/*******************************************************************************
 * include/lt/net/x509/LTNetX509.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_NET_TLS_LTNETX509_H
#define ROKU_LT_INCLUDE_LT_NET_TLS_LTNETX509_H

#include <lt/LTTypes.h>
#include <lt/system/crypto/LTSystemCrypto.h>

LT_EXTERN_C_BEGIN

typedef_LTLIBRARY_ROOT_INTERFACE(LTNetX509, 1) {
    int (*ExtractDataFromCert)(const u8 *certData, u16 certLen, LTPublicKey *publicKey, u8 *signature, u8 **certTbs, u16 *certTbsLen);
    /**< Extract public key, signature and TBS from a X.509 certificate
     *
     * @param[in]  certData    The certificate data
     * @param[in]  certLen     The certificate length
     * @param[out] publicKey   The public key inside the certificate
     * @param[out] signature   The signature of the certificate
     * @param[out] certTbs     The certificate TBS section start
     * @param[out] certTbsLen  The certificate TBS section length
     * @return  Error code
     */

    int (*ParseValidateCertChain)(const u8 *certChain, u16 certChainLen, const LTPublicKey *caKeys, u8 caKeyCount, const char *serverName, LTPublicKey *publicKey);
    /**< Parse and validate a X.509 certificate chain in DER format. If the chain is valid, then extract the public key of the end certificate.
     *
     * @param[in]  certChain     The buffer holding the certificate chain 
     * @param[in]  certChainLen  The length of certificate chain
     * @param[in]  caKeys        The array of CA keys to validate the certificate chain
     * @param[in]  caKeyCount    The count of CA keys
     * @param[in]  serverName    The server name to validate CN and ALT names
     * @param[out] publicKey     The output public key extracted from the end certificate
     * @return Error code
     */
} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif  // ROKU_LT_INCLUDE_LT_NET_TLS_LTNETX509_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  16-Oct-24   gallienus   created
 */
