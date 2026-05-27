/******************************************************************************
 * platforms/common/source/common/ltrun/ltrunmain.c - implementation of program ltrun
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define ROKU_LT_INCLUDE_LT_CORE_LTCORE_H
    /* define LTCore.h's include guard so including <lt/LT.h> won't drag in LTCore.h */

#include <lt/LT.h>

int main(int argc, const char ** argv) {
    argv[0] = "Common"; /* hack so drivers will work */
    return LT_Run(argc, argv);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  21-Jul-19   augustus    created
 *  06-May-20   augustus    now in C
 */
