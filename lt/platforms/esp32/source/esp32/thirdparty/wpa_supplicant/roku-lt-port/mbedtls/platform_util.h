/******************************************************************************
 * platform_util.h                            ESP32-S3 WPA supplicant port
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

/*
 * Placeholder.  Two files in this component - port/os_xtensa.c and
 * src/crypto/crypto_mbedtls-rsa.c - include an mbedTLS header outside the
 * USE_MBEDTLS_CRYPTO guard that wraps every use of it, so the include has to
 * resolve even though this build has no mbedTLS.  Nothing is declared here; if
 * the component is ever switched to the mbedTLS crypto branch this stops the
 * build rather than quietly shadowing the real header.
 */

#ifdef USE_MBEDTLS_CRYPTO
#error "roku-lt-port/mbedtls is an empty placeholder - drop it from the include path and supply real mbedTLS headers"
#endif
