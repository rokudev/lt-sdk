/*******************************************************************************
 * platforms/linux/source/linux/driver/crypto/LinuxDriverCryptoEntropy.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/driver/crypto/LTDriverCrypto.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/**
 * @brief  Get entropy bytes
 *
 * @param entropy     The buffer to hold the entropy bytes
 * @param entropyLen  The length of entropy to read
 */
bool Linux_GetEntropy(u8 * entropy, LT_SIZE entropyLen) {
    if (!entropy || !entropyLen) return false;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false; // fail to open
    int c = 0, n = 0;
    while (entropyLen > 0 && c < 3) {
        n = read(fd, entropy, entropyLen);
        if (n < 0 || (LT_SIZE)n > entropyLen) break; // failed to read
        entropyLen -= (LT_SIZE)n;
        entropy += (LT_SIZE)n;
        c++; // read at most 3 times
    }
    close(fd);
    return (entropyLen == 0);
}

typedef_LTObjectImpl(LTDriverCryptoEntropy, LTDriverCryptoEntropyImpl) {
} LTOBJECT_API;

static bool LTDriverCryptoEntropyImpl_GetEntropy(u8 *entropy, LT_SIZE entropyLen) {
    return Linux_GetEntropy(entropy, entropyLen);
}

static void LTDriverCryptoEntropyImpl_DestructObject(LTDriverCryptoEntropyImpl *instance) {
    LT_UNUSED(instance);
}

static bool LTDriverCryptoEntropyImpl_ConstructObject(LTDriverCryptoEntropyImpl *instance) {
    LT_UNUSED(instance);
    return true;
}

define_LTObjectImplPublic(LTDriverCryptoEntropy, LTDriverCryptoEntropyImpl,
    GetEntropy,
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  02-Mar-22   gallienus   created
 *  05-May-25   gallienus   refactor to objects.
 */
