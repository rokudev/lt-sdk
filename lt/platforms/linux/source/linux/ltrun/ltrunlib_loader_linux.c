/******************************************************************************
 * platforms/linux/source/linux/ltrun/ltrunllib_loader_linux.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#define _GNU_SOURCE
#include <dlfcn.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ltrunlib_loader.h"

// Get the directory in which the binary executed by this process resides.
static const char *
LTRunLib_GetExecDirectory(void) {
    static char s_exeDir[PATH_MAX];

    if (s_exeDir[0])
        return s_exeDir;

    char *p_exePath = malloc(PATH_MAX);
    if (!p_exePath)
        return NULL;
    ssize_t bytes = readlink("/proc/self/exe", p_exePath, PATH_MAX);
    if (bytes < 0 || bytes >= (ssize_t)PATH_MAX) {
        free(p_exePath);
        return NULL;
    }
    p_exePath[bytes] = '\0';

    strcpy(s_exeDir, dirname(p_exePath));
    free(p_exePath);

    return s_exeDir;
}

// Load the given shared object from the program directory
static void *
LTRunLib_LoadSharedObject(const char *filename) {
    const char * pExeDir = LTRunLib_GetExecDirectory();
    if (!pExeDir)
        return NULL;

    char * pDLPath = NULL;
    if (asprintf(&pDLPath, "%s/%s", pExeDir, filename) < 0 || !pDLPath)
        return NULL;

    void *handle = dlopen(pDLPath, RTLD_NOW | RTLD_LOCAL);

    free(pDLPath);
    return handle;
}

/******************************************
 * file ltrunllib_loader_linux.c #defines *
 ******************************************/
#define LTCORE_SO_LIBRARYNAME "libLTCore.so"

/**********************************
 * ltrunllib_loader API functions *
 **********************************/
bool LTRunLib_LoadLTCoreLibrary(const char * pProgramName, void ** pLTCoreLibraryHandleToSet, LTRunProc ** pLTRunProcToSet, LTGetCoreProc ** pLTGetCoreProcToSet) {
    const char * pError = NULL;

    do
    {
        *pLTCoreLibraryHandleToSet = NULL;
        if (NULL == (*pLTCoreLibraryHandleToSet = LTRunLib_LoadSharedObject(LTCORE_SO_LIBRARYNAME))) {
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
