/******************************************************************************
 * lt/source/core/LTLibraryManager.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTLIBRARYMANAGER_H
#define ROKU_LT_SOURCE_LT_CORE_LTLIBRARYMANAGER_H

#include <lt/LTTypes.h>
#include <lt/core/bsp/LTCoreBSP.h>

/****************************************************************
 * LTLibraryManager functions for LTCore private implementation *
 ****************************************************************/
void         LTLibraryManager_Init(void);
void         LTLibraryManager_Fini(void);
void         LTLibraryManager_AcceptBSP(const LTCoreBSP * pBSP);
void         LTLibraryManager_RelinquishBSP(void);
LTLibrary *  LTLibraryManager_OpenLibrary(const char * pLibraryName);
void         LTLibraryManager_CloseLibrary(LTLibrary * pLibrary);
const char * LTLibraryManager_GetGenesisLibraryName(void);
bool         LTLibraryManager_GetLibraryBuildVersionString(const char * pLibraryName, char * pBuildVersionToSet, u32 nBuildVersionBuffSize);
bool         LTLibraryManager_IsLibraryOpen(const char * pLibraryName);
bool         LTLibraryManager_GetLibrarySnapshot(const char * pLibraryName, LTCore_LibrarySnapshot * pSnapshotToFill);
void         LTLibraryManager_SnapshotOpenLibraries(LTCore_LibrarySnapshotCallbackProc * pCallback, void * pClientData);
bool         LTLibraryManager_EnumerateInstalledLibraries(LTCore_InstalledLibrariesEnumProc * pEnumProc, void * pClientData);
void         LTLibraryManager_ReportLibraryLoaderFunctionFailure(const char * pFunctionName, const char * pSalientArgument, const char * pError);

#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTLIBRARYMANAGER_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  13-Jul-19   augustus    created to isolate and make common platform independent portions
 *                          of library management
 *  22-Jul-19   augustus    replaced genesis libraries with singular genesis lib name
 *  21-May-20   augustus    converted to C
 *  23-Jan-21   augustus    added SnapshotOpenLibraries and GetLibrarySnapshot
 *  25-Jan-21   augustus    added EnumerateInstalledLibraries
 *  23-Dec-21   augustus    added GetLibraryBuildVersionString
 */
