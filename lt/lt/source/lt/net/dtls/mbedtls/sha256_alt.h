/******************************************************************************
 * <source/lt/net/mbedtls/sha256_alt.h>                        SHA256 interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef SHA256_ALT_H
#define SHA256_ALT_H

#include <lt/system/crypto/LTSystemCrypto.h>

typedef struct mbedtls_sha256_context {
    int dummy;
    LT_SHA256_CTX *lt_ctx;
}
mbedtls_sha256_context;


#endif /* sha256_alt.h */