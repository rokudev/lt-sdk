/*******************************************************************************
 * lt-firmware-example/source/eyeclops/EyeClopsInit.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * EyeClops LibInit creates a main thread that creates the EyeClops object.
 * The EyeClops object manages the show, handling all bringup in its constructor
 * and teardown in its destructor.  The thread is created first so that EyeClops
 * is already running in its own thread in its constructor.  LibFini destroys
 * the thread, which destroys the EyeClops object.  Neat and tidy.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include "EyeClops.h"

/*____________________
 / static variables */
static EyeClops  * s_pEyeClops = NULL;

/*__________________________________
 / library init, fini and binding */
static bool EyeClops_LibInit(void) {
    s_pEyeClops = lt_createobject(EyeClops);
    return (s_pEyeClops ? true : false);
}

static void EyeClops_LibFini(void) {
    lt_destroyobject(s_pEyeClops);
    s_pEyeClops = NULL;
}

define_LTObjectLibrary(1, EyeClops_LibInit, EyeClops_LibFini);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-26   augustus    created
 */
