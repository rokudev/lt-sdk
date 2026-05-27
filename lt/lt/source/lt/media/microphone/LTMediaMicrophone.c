/******************************************************************************
 * LTMediaMicrophone.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/


#include <lt/LT.h>

/*___________________________________________
  LTMediaMicrophone Root interface binding */
static bool LTMediaMicrophone_LibInit(void) {
    return true;
}

static void LTMediaMicrophone_LibFini(void) {
}

define_LTObjectLibrary(1, LTMediaMicrophone_LibInit, LTMediaMicrophone_LibFini);

