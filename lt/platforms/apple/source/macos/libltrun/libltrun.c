/******************************************************************************
 * platforms/macos/source/macos/libltrun/libltrun.c - static lib LT_Run and LT_GetCore
 *
 * _______
 * Purpose:
 * This file implements the function LT_Run() which gets packaged into a very
 * small ltrun static link library, e.g. "libltrun.a" on Linux/Macos and "ltrun.lib"
 * on Windows.  This static link library is only used on host operating systems
 * that support runtime dynamic loading and its sole purpose is to find the
 * LTCore library, load it, and vector into its version of the LT_Run() function.
 *
 * LT_GetCore() is also provided for the case where non-LT threads running in
 * the host executable desire access to the LTCore library.
 *
 * This version of LT_Run() is not needed when LT is the master device OS or on
 * host operating systems that don't support runtime dynamic loading because the
 * LTCore library in those cases is already bound into the binary executable
 * image.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include "libltrun_loader.h"

/*****************************
 * libltrun static variables */
static LTGetCoreProc *  s_pLTGetCoreProc = NULL;

/***************************
 * LT_Run() implementation */
int LT_Run(int argc, const char ** argv)
{
    int             nRetVal = -1;
    void *          pLibCore = NULL;
    LTRunProc *     pRunLTProc = NULL;
    const char *    pProgramName = argc ? argv[0] : NULL;

    if (pProgramName && *pProgramName) {
        char ch; const char * pLastSlash = NULL; while ((ch = *pProgramName++)) if (ch == '/' || ch == '\\') pLastSlash = pProgramName;
        if (pLastSlash && *pLastSlash) pProgramName = pLastSlash; else pProgramName = argv[0];
    } else pProgramName = "ltrun";

    if (LTRunLib_LoadLTCoreLibrary(pProgramName, &pLibCore, &pRunLTProc, &s_pLTGetCoreProc)) {
        nRetVal = (*pRunLTProc)(argc, argv);
        LTRunLib_UnloadLTCoreLibrary(pLibCore);
    }

    return nRetVal;
}

LTCore * LT_ISR_SAFE
LT_GetCore(void)
{
    static LTCore * s_pLTCore = NULL;
    return s_pLTCore ? s_pLTCore : (s_pLTCore = s_pLTGetCoreProc());
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  21-Jul-19   augustus    created
 *  06-May-20   augustus    now in C
 */
