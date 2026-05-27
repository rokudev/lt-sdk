/*******************************************************************************
 * platforms/macos/source/macos/driver/crypto/MacOSDriverCryptoEntropy.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include "MacOSDriverCrypto.h"

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
void MacOS_GetEntropy(u8 * entropy, LT_SIZE entropyLen) {
    if (!entropy || !entropyLen) return;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return; // fail to open
    int c = 0, n = 0;
    while (entropyLen > 0 && c < 3) {
        n = read(fd, entropy, entropyLen);
        if (n < 0 || (LT_SIZE)n > entropyLen) break; // failed to read
        entropyLen -= (LT_SIZE)n;
        entropy += (LT_SIZE)n;
        c++; // read at most 3 times
    }
    close(fd);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  02-Mar-22   gallienus   created
 *  03-Jul-23   constantine copied from LinuxDriverCryptoEntropy
 */
