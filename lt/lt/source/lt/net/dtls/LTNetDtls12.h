/*******************************************************************************
 * source/lt/net/dtls/LTNetDtls12.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/


#ifndef ROKU_LT_SOURCE_LT_NET_DTLS_LTNETDTLS12_H
#define ROKU_LT_SOURCE_LT_NET_DTLS_LTNETDTLS12_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

#include <lt/core/LTTime.h>
#include <lt/core/LTThread.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/net/core/LTNetCore.h>

#include "mbedtls/mbedtls_config.h"

#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/timing.h"


/* All options are created by applications, and passed to socket spec.
 * DTLS stack will copy the options upon socket creation.
 * Example:
 *     LTDtlsOptions dtlsOps;
 *     lt_memset(&dtlsOps, 0, sizeof(LTDtlsOptions))
 */
typedef struct LTDtlsOptions {
    u8           *clientCertificate;     ///< Optional, client certificate (whole chain).
    u16           clientCertificateLen;  ///< Optional, length of client certificate, in bytes.
    LTPublicKey  *clientPublicKey;       ///< Optional, client public key.
    LTDriverCrypto_KeyReference clientPrivateKeyReference;    ///< Optional, client private key reference.
} LTDtlsOptions;

typedef struct dtls_srtp_keys {
    unsigned char master_secret[48];
    unsigned char randbytes[64];
    mbedtls_tls_prf_types tls_prf_type;
} dtls_srtp_keys;

/* TLS session context */
typedef struct LTDtlsSessionContext {
    LTThread                        hThread;        /**< Thread handle of the session, shall not be destroyed by session.   \
                                                         But need to kill the handle's TLS timer when session is destroyed. */
    mbedtls_ssl_context             sslCtx;
    mbedtls_ssl_config              conf;
    mbedtls_entropy_context         entropy;
    mbedtls_x509_crt                clientCert;
    mbedtls_pk_context              pkey;
    dtls_srtp_keys                  dtls_srtp_keying;
    mbedtls_ssl_srtp_profile        profile[MBEDTLS_TLS_SRTP_MAX_PROFILE_LIST_LENGTH];
} LTDtlsSessionContext;

LT_EXTERN_C_END
#endif  // ROKU_LT_SOURCE_LT_NET_DTLS_LTNETDTLS12_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  13-Aug-23   valerian    created
 */
