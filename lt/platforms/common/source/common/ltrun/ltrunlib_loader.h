/******************************************************************************
 * platforms/linux/source/linux/ltrun/ltrunlib_loader.h
 *                                                 Initial loader for ltrun.lib
 *
 * This file defines the initial loading abstraction api for the
 * static library ltrun.lib/libltrun.a.  All it knows how to do is load the
 * LTCore library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_BSP_LTRUN_LTRUNLIB__LOADER_H
#define ROKU_LT_SOURCE_LT_CORE_BSP_LTRUN_LTRUNLIB__LOADER_H

#include <lt/LTTypes.h>

/*********************
 * ltrunlib #defines */
#define LTCOREEXPORTED_LTRUN            "LTCoreExported_LTRun"
#define LTCOREEXPORTED_LTGETCORE        "LTCoreExported_LTGetCore"

/*********************
 * ltrunlib typedefs */
typedef int         (LTRunProc)(int argc, const char ** argv);
typedef LTCore *    (LTGetCoreProc)(void);

/*********************
 * ltrunlib typedefs */
bool LTRunLib_LoadLTCoreLibrary(const char * pProgramName, void ** pLTCoreLibraryHandleToSet, LTRunProc ** pLTRunProcToSet, LTGetCoreProc ** pLTGetCoreProcToSet);
void LTRunLib_UnloadLTCoreLibrary(void * pLTCoreLibraryHandle);

#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_BSP_LTRUN_LTRUNLIB__LOADER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jun-20   augustus    created
 */
