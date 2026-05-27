/******************************************************************************
 * platforms/macos/source/macos/libltrun/libltrun_loader_macos.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <dlfcn.h>
#include <stdio.h>

#include "libltrun_loader.h"

/******************************************
 * file libltrun_loader_macos.c #defines *
 ******************************************/
#define LTCORE_SO_LIBRARYNAME "libLTCore.so"

/**********************************
 * libltrun_loader API functions *
 **********************************/
bool LTRunLib_LoadLTCoreLibrary(const char * pProgramName, void ** pLTCoreLibraryHandleToSet, LTRunProc ** pLTRunProcToSet, LTGetCoreProc ** pLTGetCoreProcToSet) {
    const char * pError = NULL;

    do
    {
        *pLTCoreLibraryHandleToSet = NULL;
        if (NULL == (*pLTCoreLibraryHandleToSet = dlopen(LTCORE_SO_LIBRARYNAME, RTLD_NOW | RTLD_LOCAL))) {
            pError = "Unable to find " LTCORE_SO_LIBRARYNAME "\n";
            break;
        }
        if (NULL == (*pLTRunProcToSet = (LTRunProc *)dlsym(*pLTCoreLibraryHandleToSet, LTCOREEXPORTED_LTRUN))) {
            pError = "Unable to find function " LTCOREEXPORTED_LTRUN " in " LTCORE_SO_LIBRARYNAME "\n";
            break;
        }
        if (NULL == (*pLTGetCoreProcToSet = (LTGetCoreProc *)dlsym(*pLTCoreLibraryHandleToSet, LTCOREEXPORTED_LTGETCORE))) {
            pError = "Unable to find function " LTCOREEXPORTED_LTGETCORE " in " LTCORE_SO_LIBRARYNAME "\n";
            break;
        }
    }
    while (false);

    if (pError) {
        const char * pDLError = dlerror();
        printf("%s: %s\n", pProgramName, pDLError ? pDLError : pError);
        if (*pLTCoreLibraryHandleToSet) dlclose(pLTCoreLibraryHandleToSet);
        return false;
    }
    return true;
}

void LTRunLib_UnloadLTCoreLibrary(void * pLTCoreLibraryHandle) {
    dlclose(pLTCoreLibraryHandle);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jun-20   augustus    created original versions
 */
