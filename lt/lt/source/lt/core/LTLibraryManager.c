/******************************************************************************
 * lt/source/core/LTLibraryManager.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTCoreImpl.h"
#include "LTLibraryManager.h"
#include "LTStdlibImpl.h"
#include "LTHandle.h"
#include "LTThreadImpl.h"
#include "lt/core/LTArray.h"
#include "lt/core/bsp/LTHostAPI.h"
#include "lt/core/LTMonitor.h"

/************************************
 * file LTLibraryManager.c #defines */
#define LTLIBRARYMANAGER_LOGGER_SECTION             "ltlibrary"
#define LTLIBRARYMANAGER_OPENLIBRARY_FORMAT_STRING  "LTLibrary_%s_OpenLibrary"
#define LTLIBRARYMANAGER_CLOSELIBRARY_FORMAT_STRING "LTLibrary_%s_CloseLibrary"

#define LIBINIT_STACKSIZE                           1280  /* temporary until I read the stacksize from the library */
#define USE_THREADED_LIBOPEN                        1

/*************************************************
 * LOGGING CONTROL - LIBLOG_OPEN and LIBLOG_LOAD */
#ifdef  LT_DEBUG
   #define LIBLOG_OPEN          LTLOG_LOGNULL
   #define LIBLOG_OPENED        LTLOG_LOGNULL
   #define LIBLOG_CLOSE         LTLOG_LOGNULL
   #define LIBLOG_CLOSED        LTLOG_LOGNULL
   #define LIBLOG_LOAD          LTLOG_LOGNULL
   #define LIBLOG_LOADED        LTLOG_LOGNULL
   #define LIBLOG_UNLOAD        LTLOG_LOGNULL
   #define LIBLOG_UNLOADED      LTLOG_LOGNULL
   #define LIBLOG_FAIL          LTLOG_LOGNULL
   #define LIBLOG_LOADSCOPED    1
#else
   #define LIBLOG_OPEN          LTLOG_LOGNULL
   #define LIBLOG_OPENED        LTLOG_LOGNULL
   #define LIBLOG_CLOSE         LTLOG_LOGNULL
   #define LIBLOG_CLOSED        LTLOG_LOGNULL
   #define LIBLOG_LOAD          LTLOG_LOGNULL
   #define LIBLOG_LOADED        LTLOG_LOGNULL
   #define LIBLOG_UNLOAD        LTLOG_LOGNULL
   #define LIBLOG_UNLOADED      LTLOG_LOGNULL
   #define LIBLOG_FAIL          LTLOG_LOGNULL
   #define LIBLOG_LOADSCOPED    1
#endif

#define LIBLOG_INDENT           "%-22s"

DEFINE_LTLOG_SECTION(LTLIBRARYMANAGER_LOGGER_SECTION);

/*****************************************
 * file LTLibraryManager.c private types */
enum LTLibraryManager_OpenState {
    kLTLibraryManager_OpenStateInvalid      = 0,
    kLTLibraryManager_OpenStateOpened       = 1,
    kLTLibraryManager_OpenStateOpening      = 2,
    kLTLibraryManager_OpenStateOpenFailed   = 3,
    kLTLibraryManager_OpenStateClosing      = 4
};

typedef struct OpenCloseLibContext {
    LTHandle                                hThread;
    LTMonitor                             * pMonitor;                 /* thread opening lib waits on this monitor for LibInit to complete */
    LTLibrary_OpenLibrary_Proc            * pOpenLibraryProc;
    LTLibrary_CloseLibrary_Proc           * pCloseLibraryProc;
    LTLibrary                             * pLibrary;
    struct OpenCloseLibContext            * pPrevOpenCloseLibContext; /* set when open/close chained from LibInit/Fini() of other library */
    bool                                    bWorkComplete;
} OpenCloseLibContext;

typedef struct LoadedLibraryRecord {
    void                                  * pNativeLibraryHandle;
    LTLibrary_CloseLibrary_Proc           * pCloseLibraryProc;
    LTLibrary *                             pLibrary;
    u32                                     nOpenCount;
    LTThread                                hActiveThread; /* starts out as the thread that initiated the open
                                                              becomes the library's LibInit/LibFini thread */
    enum LTLibraryManager_OpenState         openState;
} LoadedLibraryRecord;

typedef struct {
    LTCore_LibrarySnapshotCallbackProc    * pCallback;
    void                                  * pClientData;
    LTCore_LibrarySnapshot                  snapshot;
} SnapshotClientData;

/********************************************
 * file LTLibraryManager.c static variables */
static ILTThread           * iThread                = NULL;
static const LTCoreBSP     * s_pBSP                 = NULL;
static LTMutex             * s_mutex                = NULL;
static LTAssociativeArray  * s_loadedLibraries      = NULL;                /* only access with s_mutex locked */
static LoadedLibraryRecord * s_pRecord              = NULL;                /* static to reduce stack usage; only access with s_mutex locked */
static LoadedLibraryRecord   s_record;                                     /* static to reduce stack usage; only access with s_mutex locked */
static char                  s_libraryName[kLTLibrary_MaxNameBufferSize];  /* static to reduce stack usage; only access with s_mutex locked */
static char                  s_genesisLibraryName[kLTLibrary_MaxNameBufferSize];
static const char          * s_pThreadSpecificID = "LTLibOC";

LTLibrary_LTObjectMapEntry * g_pStaticallyBoundLTObjectMapEntries = NULL;

#ifdef LT_NO_DYNAMIC_LOADER
/*******************************************************************************
 * file LTLibraryManager.c statically bound LTLibrary 'loading' implementation */
LTStaticallyBoundLibraryEntry * g_pStaticallyBoundLTLibraryEntries = NULL;
static bool StaticallyBoundLibraryPseudoLoad(const char * pLibName, void ** ppHandleToSet, LTLibrary_OpenLibrary_Proc ** ppOpenLibraryProcToSet, LTLibrary_CloseLibrary_Proc ** ppCloseLibraryProcToSet)
{
    LTStaticallyBoundLibraryEntry * pCurr = g_pStaticallyBoundLTLibraryEntries;
    LTStaticallyBoundLibraryEntry * pFound = NULL;
    while (pCurr) {
       if (0 == LTStdlibImpl_strcmp(pCurr->pLibName, pLibName)) { pFound = pCurr; break; }
        pCurr = pCurr->pNextEntry;
    }
    if (pFound) {
        *ppHandleToSet = (void *)pFound;
        *ppOpenLibraryProcToSet = pFound->pOpenLibraryFunc;
        *ppCloseLibraryProcToSet = pFound->pCloseLibraryFunc;
        return true;
    }
    return false;
}
#endif

/****************************************************************
 * LTLibraryManager functions for LTCore private implementation */
void
LTLibraryManager_Init(void) {
    LTCore * pCore    = LTCoreImpl_GetLTCore();
    s_mutex           = lt_createobject(LTMutex);
    iThread           = LTCoreImpl_GetILTThread();
    s_loadedLibraries = LTAssociativeArray_CreateStructArray(sizeof(LoadedLibraryRecord));
    s_loadedLibraries->API->TuneAllocation(s_loadedLibraries, 8, 2, 16);

    // suss out the genesis library name
    int argc = pCore->GetArgc();
    const char ** argv = pCore->GetArgv();
    const char * pGenesisLibName = NULL;
    for (int i = 1; i < argc; i++) {
        const char * pArg = argv[i];
        if (pArg && *pArg && (*pArg != '-')) {
            pGenesisLibName = pArg;
            break;
        }
    }
    if (NULL != pGenesisLibName) pCore->GetLTStdlib()->strncpyTerm(s_genesisLibraryName, pGenesisLibName, sizeof(s_genesisLibraryName));
    else s_genesisLibraryName[0] = 0;
}

static
bool UnloadLibraryEnumProc(LTAssociativeArray * pArray, const void * pKey, const u16 keySize, void * pValue, void * pClientData) {
    LT_UNUSED(pArray); LT_UNUSED(keySize); LT_UNUSED(pClientData);
    LT_UNUSED(pKey);  // Retained in case LTLOGs are excised

    struct LoadedLibraryRecord * pLibRec = (struct LoadedLibraryRecord *) pValue;
    LTLOG("fini", "%s still loaded, ref count %d, unloading", (char *)pKey, (int)pLibRec->nOpenCount);

    #ifdef LT_NO_DYNAMIC_LOADER
        /* nothing to do */
    #else
        s_pBSP->hostAPI->LibraryUnload(pLibRec->pNativeLibraryHandle);
    #endif

    return true; // keep going to the end
}

void LTLibraryManager_AcceptBSP(const struct LTCoreBSP * pBSP) {
    s_pBSP = pBSP;
}

void LTLibraryManager_RelinquishBSP(void) {
    s_pBSP = NULL;
}

void
LTLibraryManager_Fini(void) {
    s_mutex->API->Lock(s_mutex);
    s_genesisLibraryName[0] = 0;
    s_loadedLibraries->API->Enumerate(s_loadedLibraries, UnloadLibraryEnumProc, NULL);
    lt_destroyobject(s_loadedLibraries);
    s_mutex->API->Unlock(s_mutex); lt_destroyobject(s_mutex); s_mutex = NULL;
    iThread  = NULL;
}

static struct LoadedLibraryRecord * LookupLibLoadedRecordByName(const char * pLibraryName) {
    struct LoadedLibraryRecord * pRecord = LTCStringKeyedArray_Get(s_loadedLibraries, pLibraryName, NULL);
    return pRecord;
}

static void
LTLibraryManager_FillSnapshotInternal(struct LoadedLibraryRecord * pRecord, const char * pLibraryName, LTCore_LibrarySnapshot * pSnapshotToFill) {
    // function arguments are pre-validated: pRecord is non-null with openState Open, pSnapShotToFill is non-null with valid nStructureSize
    LTStdlibImpl_strncpyTerm(pSnapshotToFill->name, pLibraryName, sizeof(pSnapshotToFill->name));
    LTStdlibImpl_strncpyTerm(pSnapshotToFill->rootInterfaceName, pRecord->pLibrary->GetInterfaceName(), sizeof(pSnapshotToFill->rootInterfaceName));
    pSnapshotToFill->nRootInterfaceVersion = pRecord->pLibrary->GetInterfaceVersion();
    pSnapshotToFill->rootInterfaceType = pRecord->pLibrary->GetInterfaceType();
    pSnapshotToFill->nOpenCount = (u16)pRecord->nOpenCount;
}

static void
LTLibraryManager_FillSnapshotWithLTCore(LTCore_LibrarySnapshot * pSnapshotToFill) {
    // fpSnapShotToFill is non-null with valid nStructureSize
    LTCore * pCore = LT_GetCore();
    LTStdlibImpl_strncpyTerm(pSnapshotToFill->name, pCore->GetInterfaceName(), sizeof(pSnapshotToFill->name));
    LTStdlibImpl_strncpyTerm(pSnapshotToFill->rootInterfaceName, pCore->GetInterfaceName(), sizeof(pSnapshotToFill->rootInterfaceName));
    pSnapshotToFill->nRootInterfaceVersion = pCore->GetInterfaceVersion();
    pSnapshotToFill->rootInterfaceType = pCore->GetInterfaceType();
    pSnapshotToFill->nOpenCount = 1;
}

static bool
LTLibraryManager_SnapshotOpenLibrariesEnumProc(LTAssociativeArray * pArray, const void * pKey, const u16 keySize, void * pValue, void * pClientData) {
    LT_UNUSED(pArray); LT_UNUSED(keySize);

    LoadedLibraryRecord * pRecord = (struct LoadedLibraryRecord *)pValue;
    SnapshotClientData * pCD = (SnapshotClientData *)pClientData;

    if (pRecord->openState == kLTLibraryManager_OpenStateOpened) {
        LTStdlibImpl_memset(&pCD->snapshot, 0, sizeof(pCD->snapshot));
        pCD->snapshot.nStructureSize = sizeof(pCD->snapshot);
        LTLibraryManager_FillSnapshotInternal(pRecord, (const char *)pKey, &pCD->snapshot);
        pCD->pCallback(&pCD->snapshot, pCD->pClientData);
    }

    return true; // keep going to the end
}

static bool
LTLibraryManager_CallOpenLibraryProc(void) {
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    OpenCloseLibContext * pContext = (OpenCloseLibContext *)LTThreadImpl_GetThreadSpecificClientData(hThread, s_pThreadSpecificID);
    pContext->pMonitor->API->Enter(pContext->pMonitor);
    pContext->pLibrary = pContext->pOpenLibraryProc(LT_GetCore());
    pContext->bWorkComplete = true;
    pContext->pMonitor->API->Notify(pContext->pMonitor);
    pContext->pMonitor->API->Exit(pContext->pMonitor);
    return false;
}

static bool
LTLibraryManager_CallCloseLibraryProc(void) {
    LTThread hThread = LTThreadImpl_GetCurrentThread();
    OpenCloseLibContext * pContext = (OpenCloseLibContext *)LTThreadImpl_GetThreadSpecificClientData(hThread, s_pThreadSpecificID);
    pContext->pMonitor->API->Enter(pContext->pMonitor);
    pContext->pCloseLibraryProc(pContext->pLibrary);
    pContext->bWorkComplete = true;
    pContext->pMonitor->API->Notify(pContext->pMonitor);
    pContext->pMonitor->API->Exit(pContext->pMonitor);
    return false;
}

static OpenCloseLibContext *
LTLibraryManager_CreateOpenCloseLibContext(const char * pLibraryName, LTLibrary_OpenLibrary_Proc * pOpenLibraryProc, LTLibrary_CloseLibrary_Proc * pCloseLibraryProc, LTLibrary * pLibrary,  u32 nThreadStackSize) {
    OpenCloseLibContext * pContext = (OpenCloseLibContext *)lt_malloc(sizeof(*pContext));
    u8 currentPriority;
    do {
        if (NULL == pContext) {
            LTLOG_YELLOWALERT("openlibrary.fail.null", "malloc context failed for \"%s\"", pLibraryName);
            break;
        }
        /* borrow pContext->hThread to get the current thread priority.  Note the current thread might be 0
           if lt_openlibrary() is called from a non-LT thread, e.g. from function main() in an LT build tool */
        pContext->hThread = LTThreadImpl_GetCurrentThread();
        currentPriority = pContext->hThread ? LTThreadImpl_GetPriority(pContext->hThread) : 0;
        if (currentPriority > (kLTThread_PriorityHighest - 1)) currentPriority = (kLTThread_PriorityHighest - 1);

        pContext->hThread = 0; /* explicitly set to 0 before monitor create for error checking after break; */
        if (NULL == (pContext->pMonitor = lt_createobject(LTMonitor))) {
            LTLOG_YELLOWALERT("openlibrary.fail.mon", "context monitor create failed for \"%s\"", pLibraryName);
            break;
        }

        // create the thread - for thread name prepend "Opn~" for open or "Cls~" for close onto the library name
        lt_strncpyTerm(s_libraryName, pOpenLibraryProc ? "Opn~" : "Cls~" , sizeof(s_libraryName));
        pContext->hThread = 4; // borrow pContext->hThread temporarily for prefix size
        lt_strncpyTerm(s_libraryName + pContext->hThread, pLibraryName, sizeof(s_libraryName) - pContext->hThread);
        if (0 == (pContext->hThread = LTThreadImpl_CreateThread(s_libraryName))) {
            LTLOG_YELLOWALERT("openlibrary.fail.thread", "thread create failed for \"%s\"", pLibraryName);
            break;
        }

        LTThreadImpl_SetStackSize(pContext->hThread, nThreadStackSize);
        LTThreadImpl_SetPriority(pContext->hThread, currentPriority);
        LTThreadImpl_SetThreadSpecificClientData(pContext->hThread, s_pThreadSpecificID, NULL, pContext);
        pContext->pOpenLibraryProc = pOpenLibraryProc;
        pContext->pCloseLibraryProc = pCloseLibraryProc;
        pContext->pLibrary = pLibrary;
        pContext->bWorkComplete = false;

    }
    while (false);
    if (pContext && (0 == pContext->hThread)) {
        if (pContext->pMonitor) lt_destroyobject(pContext->pMonitor);
        lt_free(pContext);
        pContext = NULL;
    }
    return pContext;
}

LTLibrary *
LTLibraryManager_OpenLibrary(const char * pLibraryName) {

#ifndef LT_NO_DYNAMIC_LOADER
    static char s_libraryName[128];
#endif
    if (NULL == pLibraryName || (0 == *pLibraryName)) return NULL;
    if (0 == lt_strcasecmp("LTCore", pLibraryName)) return (LTLibrary *)LT_GetCore();
    if (lt_strlen(pLibraryName) > kLTLibrary_MaxNameLen) {
        LIBLOG_OPEN("open", "\"%s\" too long; must be < %d chars", pLibraryName, (int)kLTLibrary_MaxNameBufferSize);
        return NULL;
    }

    LIBLOG_OPEN(".....open", "%s", pLibraryName);
    // first see if our library is already loaded
    LTLibrary * pLibrary = NULL;        /* can't be static, accessed outside mutex */
    const char * pRetryReason = NULL;   /* can't be static, accessed outside mutex */
    u32 nTries = 0;                     /* can't be static, accessed outside mutex */
    s_mutex->API->Lock(s_mutex);
    while (NULL != (s_pRecord = LookupLibLoadedRecordByName(pLibraryName))) {
        switch (s_pRecord->openState) {
            case kLTLibraryManager_OpenStateOpened:
                // normal case
                LT_ASSERT(0 != s_pRecord->nOpenCount);
                s_pRecord->nOpenCount++;
                pLibrary = s_pRecord->pLibrary;
                LIBLOG_OPENED("...opened", LIBLOG_INDENT "---> refcount %d", pLibraryName, (int)s_pRecord->nOpenCount);
                s_mutex->API->Unlock(s_mutex);
                LT_ASSERT(NULL != pLibrary);
                return pLibrary;
            case kLTLibraryManager_OpenStateOpening:
                LT_ASSERT(0 != s_pRecord->hActiveThread);
                if (iThread->GetCurrentThread() == s_pRecord->hActiveThread) {
                    LTLOG_YELLOWALERT("recursiveopen", LIBLOG_INDENT "---> recursive open in progress", pLibraryName);
                    s_mutex->API->Unlock(s_mutex);
                    return NULL;
                }
                pRetryReason = "opened";
                break;
            case kLTLibraryManager_OpenStateClosing:
                LT_ASSERT(0 != s_pRecord->hActiveThread);
                if (iThread->GetCurrentThread() == s_pRecord->hActiveThread) {
                    LTLOG_YELLOWALERT("openduringclose", LIBLOG_INDENT "---> being opened during close", pLibraryName);
                    s_mutex->API->Unlock(s_mutex);
                    return NULL;
                }
                pRetryReason = "closed";
                break;
            default:
                LT_ASSERT(0);
                LT_UNUSED(pRetryReason);
                break;
        }
        s_mutex->API->Unlock(s_mutex);
#if 1
        enum { kLogTries = 4, kGiveupTries = 0x4FFFFFF, kRetryWaitMS = 40 };
        /* this new code logs as before if a library open wait time exceeds 160ms - it only prints once and gives
           the library load approximately 38 days before it times out and fails as before */
        if (++nTries == kLogTries) {
            LTLOG_YELLOWALERT("collision", "\"%s\" is being %s by another thread.  Retried %lu times over %lu ms.  Continuing...",
                pLibraryName, pRetryReason, LT_Pu32(kLogTries)-1, (LT_Pu32(kLogTries)-1) * LT_Pu32(kRetryWaitMS));
        }
        if (nTries >= kGiveupTries) {
            LTLOG_YELLOWALERT("collision.giveup", "\"%s\" is being %s by another thread.  Retried %lu times over %lu ms, giving up.",
                pLibraryName, pRetryReason, LT_Pu32(kGiveupTries)-1, (LT_Pu32(kGiveupTries)-1) * LT_Pu32(kRetryWaitMS));
            return NULL;
        }
#else
        enum { kMaxTries = 4, kRetryWaitMS = 40 };
        if (++nTries >= kMaxTries) {
            LTLOG_YELLOWALERT("collision.max", "\"%s\" is being %s by another thread.  Retried %lu times over %lu ms, giving up.",
                pLibraryName, pRetryReason, LT_Pu32(kMaxTries)-1, (LT_Pu32(kMaxTries)-1) * LT_Pu32(kRetryWaitMS));
            return NULL;
        }
#endif
        iThread->Sleep(LTTime_Milliseconds(kRetryWaitMS));
        LIBLOG_OPEN("open.retry", "\"%s\" is being %s by another thread.  Retry %d", pLibraryName, pRetryReason, (int)nTries);
        s_mutex->API->Lock(s_mutex);
    }

    // try to load up the library
    LTLibrary_OpenLibrary_Proc * pOpenLibraryProc = NULL;   /* can't be static, accessed outside mutex */

    do
    {
        #ifdef LT_NO_DYNAMIC_LOADER
            if (! StaticallyBoundLibraryPseudoLoad(pLibraryName, &s_record.pNativeLibraryHandle, &pOpenLibraryProc, &s_record.pCloseLibraryProc)) break;
        #else
            // load the library and get handles to its exported functions
            if (LTHOSTAPI_LOADLIBRARY_SUCCESS != s_pBSP->hostAPI->LibraryLoad(pLibraryName, &s_record.pNativeLibraryHandle)) break;
            lt_snprintf(s_libraryName, sizeof(s_libraryName), LTLIBRARYMANAGER_OPENLIBRARY_FORMAT_STRING, pLibraryName);
            if (NULL == (pOpenLibraryProc = (LTLibrary_OpenLibrary_Proc *)s_pBSP->hostAPI->LibraryLookupSymbol(s_record.pNativeLibraryHandle, s_libraryName))) break;
            lt_snprintf(s_libraryName, sizeof(s_libraryName), LTLIBRARYMANAGER_CLOSELIBRARY_FORMAT_STRING, pLibraryName);
            if (NULL == (s_record.pCloseLibraryProc = (LTLibrary_CloseLibrary_Proc *)s_pBSP->hostAPI->LibraryLookupSymbol(s_record.pNativeLibraryHandle, s_libraryName))) break;
        #endif

        // LIBRARY exported functions are obtained, library opening proceeds, finish filling out s_record

        // s_record.pNativeLibraryHandle -> already set
        // s_record.pCloseLibraryProc    -> already set
        s_record.pLibrary                 = NULL;
        s_record.nOpenCount               = 0;
        s_record.openState                = kLTLibraryManager_OpenStateOpening;

        // DRW 19-Aug-21 : create a context for LibInit to occur in its own thread
        LTThread hThread = 0;
        OpenCloseLibContext * pContext = LTLibraryManager_CreateOpenCloseLibContext(pLibraryName, pOpenLibraryProc, NULL, NULL, LIBINIT_STACKSIZE); // LIBINIT_STACKSIZE temp until I read the stacksize from the lib
        
        // copy s_record into array
        s_record.hActiveThread = pContext ? pContext->hThread : LTThreadImpl_GetCurrentThread();
        LTCStringKeyedArray_Set(s_loadedLibraries, pLibraryName, &s_record);

        #if LIBLOG_LOADSCOPED
            LIBLOG_LOAD("....load.", LIBLOG_INDENT "---> LibInit {", pLibraryName);
        #else
            LIBLOG_LOAD("....load.", "%s", pLibraryName);
        #endif

        // unlock mutex for the duration of the open library proc invocation
        s_mutex->API->Unlock(s_mutex);
        if (pContext) {
            // we got a context for calling the open library proc in a thread for that purpose, do so
            hThread = pContext->hThread;
            pContext->pMonitor->API->Enter(pContext->pMonitor);
            LTThreadImpl_Start(hThread, &LTLibraryManager_CallOpenLibraryProc, NULL);
            pContext->pMonitor->API->Wait(pContext->pMonitor, LTTime_Infinite());
            //do { pContext->pMonitor->API->Wait(pContext->pMonitor, LTTime_Infinite()); } while (false == pContext->bWorkComplete);
            pContext->pMonitor->API->Exit(pContext->pMonitor);
            pLibrary = pContext->pLibrary;
            lt_destroyobject(pContext->pMonitor);
            lt_free(pContext);
        }
        else {
            LTLOG_YELLOWALERT("open.nothread", LIBLOG_INDENT "---> no thread for LibInit!  Using client's thread.", pLibraryName);
            pLibrary = (*pOpenLibraryProc)(LT_GetCore());
        }
        s_mutex->API->Lock(s_mutex);

        // reload s_pRecord as record location could have been changed while mutex was unlocked
        s_pRecord = LookupLibLoadedRecordByName(pLibraryName);

        if (NULL == pLibrary) {
            // before removing s_pRecord, copy s_pRecord->pNativeLibraryHandle into s_record.pNativeLibraryHandle so it can be used to
            // unload the library below; mutex is and will remain locked until after s_record.pNativeLibraryHandle is used
            s_record.pNativeLibraryHandle = s_pRecord->pNativeLibraryHandle;
            if (hThread) iThread->Destroy(hThread);
            LTCStringKeyedArray_Remove(s_loadedLibraries, pLibraryName);
            LTLOG_YELLOWALERT("null.open", LIBLOG_INDENT "---> LibInit returned false", pLibraryName);
            break;
        }

        // Update record
        s_pRecord->nOpenCount++;
        s_pRecord->openState     = kLTLibraryManager_OpenStateOpened;
        s_pRecord->hActiveThread = 0;
        s_pRecord->pLibrary      = pLibrary;

#if 0
        // figure out whether to keep the thread around or destroy it
        if (hThread) {
            if (LTThreadImpl_IsKeepAliveAfterLibInitFlagSet(hThread)) s_pRecord->hActiveThread = hThread;
            else iThread->Destroy(hThread);
        }
#else
        if (hThread) iThread->Destroy(hThread);
#endif

            
        #if LIBLOG_LOADSCOPED
            LIBLOG_LOADED("..loaded.", LIBLOG_INDENT "---> } LibInit", pLibraryName);
        #else
            LIBLOG_LOADED("..loaded.", "%s", pLibraryName);
        #endif
   }
   while (false);

   if (NULL == pLibrary) {
        // load failed
        if (NULL != s_record.pNativeLibraryHandle) {
            #ifdef LT_NO_DYNAMIC_LOADER
                /* nothing to do */
            #else
                s_pBSP->hostAPI->LibraryUnload(s_record.pNativeLibraryHandle);
            #endif
        }
        LIBLOG_FAIL("loadfail.", "%s", pLibraryName);
    }
    s_mutex->API->Unlock(s_mutex);

    return pLibrary;
}

void
LTLibraryManager_CloseLibrary(LTLibrary * pLibrary) {
    if ((NULL == pLibrary) || (pLibrary == (LTLibrary *)LT_GetCore())) return;

    // find the lib loaded record
    s_mutex->API->Lock(s_mutex);
    const char * pLibName = pLibrary->GetLibraryExtrinsicName();
    LIBLOG_CLOSE("....close", "%s", pLibName);

    s_pRecord = LookupLibLoadedRecordByName(pLibName);

    if (NULL == s_pRecord) {
        s_mutex->API->Unlock(s_mutex);
        LTLOG_YELLOWALERT("closefail", LIBLOG_INDENT "---> not opened", pLibName);
        LT_ASSERT(0);
        return;
    }

    LT_ASSERT(pLibrary == s_pRecord->pLibrary);
    LT_ASSERT(0 != s_pRecord->nOpenCount);
    LT_ASSERT(0 == s_pRecord->hActiveThread);
    if (kLTLibraryManager_OpenStateOpened != s_pRecord->openState) {
        s_mutex->API->Unlock(s_mutex);
        LTLOG_YELLOWALERT("closefail", LIBLOG_INDENT "---> not opened", pLibName);
        LT_ASSERT(0);
        return;
    }

    // decrement the open count
    s_pRecord->nOpenCount--;
    if (0 == s_pRecord->nOpenCount) {
        // open count is zero; unload the library
        // cache close proc
        LTLibrary_CloseLibrary_Proc * pCloseLibraryProc = s_pRecord->pCloseLibraryProc;
        #ifndef LT_NO_DYNAMIC_LOADER
            void * pNativeLibraryHandle = s_pRecord->pNativeLibraryHandle;
        #endif
        s_pRecord->openState = kLTLibraryManager_OpenStateClosing;

        // DRW 21-Aug-21 : create a context for LibFini to occur in its own thread, and create a monitor for waiting
        OpenCloseLibContext * pContext = LTLibraryManager_CreateOpenCloseLibContext(pLibName, NULL, pCloseLibraryProc, pLibrary, LIBINIT_STACKSIZE); // LIBINIT_STACKSIZE temp until I read the stacksize from the lib
        s_pRecord->hActiveThread = pContext ? pContext->hThread : iThread->GetCurrentThread();

        #if LIBLOG_LOADSCOPED
            LIBLOG_UNLOAD("..unload.", LIBLOG_INDENT "---> LibFini {", pLibName);
        #else
            LIBLOG_UNLOAD("..unload.", "%s", pLibName);
        #endif

        // unlock mutex for the duration of the close library proc invocation
        s_mutex->API->Unlock(s_mutex);
        if (pContext) {
            // got a thread context for invoking the close proc; invoke in the thread
            pContext->pMonitor->API->Enter(pContext->pMonitor);
            LTThreadImpl_Start(pContext->hThread, &LTLibraryManager_CallCloseLibraryProc, NULL);
            pContext->pMonitor->API->Wait(pContext->pMonitor, LTTime_Infinite());
            //do { pContext->pMonitor->API->Wait(pContext->pMonitor, LTTime_Infinite()); } while (false == pContext->bWorkComplete);
            pContext->pMonitor->API->Exit(pContext->pMonitor);
            // destroy the thread and the monitor and free the context

            LTHandle_DestroyHandle(pContext->hThread);
            lt_destroyobject(pContext->pMonitor);
            lt_free(pContext);
        }
        else {
            // couldn't create the context; close the library in the current thread
            LTLOG_YELLOWALERT("close.nothread", LIBLOG_INDENT "---> no thread for LibFini!  Using client's thread.", pLibName);
            (*pCloseLibraryProc)(pLibrary);
        }
        // lock the mutex to unload the library and remove the record
        s_mutex->API->Lock(s_mutex);

        // unload the library and remove
        lt_strncpyTerm(s_libraryName, pLibName, sizeof(s_libraryName)); // copy pLibName before unloading
        #ifdef LT_NO_DYNAMIC_LOADER
            /* nothing to do */
        #else
            /* unload the library */
            s_pBSP->hostAPI->LibraryUnload(pNativeLibraryHandle);
        #endif
        LTCStringKeyedArray_Remove(s_loadedLibraries, s_libraryName);
        #if LIBLOG_LOADSCOPED
            LIBLOG_UNLOADED("unloaded.", LIBLOG_INDENT "---> } LibFini", s_libraryName);
        #else
            LIBLOG_UNLOADED("unloaded.", "%s", s_libraryName);
        #endif
        LIBLOG_CLOSED("...closed", LIBLOG_INDENT "---> refcount 0", s_libraryName);
    }
    else {
        LIBLOG_CLOSED("...closed", LIBLOG_INDENT "---> refcount %d", pLibName, (int)s_pRecord->nOpenCount);
    }
    s_mutex->API->Unlock(s_mutex);
}

const char *
LTLibraryManager_GetGenesisLibraryName(void) {
    return s_genesisLibraryName;
}

bool LTLibraryManager_GetLibraryBuildVersionString(const char * pLibraryName, char * pBuildVersionStringToSet, u32 nBuildVersionStringBuffSize) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    if ((NULL == pLibraryName) || (0 == *pLibraryName) || (NULL == pBuildVersionStringToSet) || (0 == nBuildVersionStringBuffSize)) return false;
    const char * pLibraryBuildVersion = NULL;
    int nLen = 0;
    /* special case LTCore */
    if (0 == lt_strcmp(pLibraryName,"LTCore")) {
        pLibraryBuildVersion = LT_GetCore()->GetLibraryBuildVersion();
        if (pLibraryBuildVersion) {
            nLen = lt_strlen(pLibraryBuildVersion);
            if (nBuildVersionStringBuffSize > (u32)nLen) {
                lt_strncpyTerm(pBuildVersionStringToSet, pLibraryBuildVersion, nBuildVersionStringBuffSize);
                return true;
            }
        }
        return false;
    }

    /* get the library open record */
    bool bRetVal = false;
    s_mutex->API->Lock(s_mutex);
    struct LoadedLibraryRecord * pRecord = LookupLibLoadedRecordByName(pLibraryName);
    if (pRecord && (pRecord->openState == kLTLibraryManager_OpenStateOpened)) {
        pLibraryBuildVersion = pRecord->pLibrary->GetLibraryBuildVersion();
        if (pLibraryBuildVersion) {
            nLen = lt_strlen(pLibraryBuildVersion);
            if (nBuildVersionStringBuffSize > (u32)nLen) {
                lt_strncpyTerm(pBuildVersionStringToSet, pLibraryBuildVersion, nBuildVersionStringBuffSize);
                bRetVal = true;
            }
        }
    }
    s_mutex->API->Unlock(s_mutex);
    return bRetVal;
}

bool
LTLibraryManager_IsLibraryOpen(const char * pLibraryName) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();

    // pre-check for bogus name and LTCore
    if ((NULL == pLibraryName) || (0 == *pLibraryName)) return false;
    if (0 == lt_strcmp(pLibraryName, "LTCore")) return true;

    // look 'er up
    s_mutex->API->Lock(s_mutex);
    struct LoadedLibraryRecord * pRecord = LookupLibLoadedRecordByName(pLibraryName);
    bool bOpen = (pRecord && (pRecord->openState == kLTLibraryManager_OpenStateOpened));
    s_mutex->API->Unlock(s_mutex);
    return bOpen;
}

bool
LTLibraryManager_GetLibrarySnapshot(const char * pLibraryName, LTCore_LibrarySnapshot * pSnapshotToFill) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    /* check to make sure we have a valid pLibrarName and a valid snapshot to fill */
    if ((NULL == pLibraryName) || (0 == *pLibraryName) || (NULL == pSnapshotToFill) || (pSnapshotToFill->nStructureSize != sizeof(*pSnapshotToFill))) return false;

    /* special case LTCore */
    if (0 == lt_strcmp(pLibraryName, "LTCore")) {
        LTLibraryManager_FillSnapshotWithLTCore(pSnapshotToFill);
        return true;
    }

    /* zero pSnapshot to fill */
    LTStdlibImpl_memset(pSnapshotToFill, 0, sizeof(*pSnapshotToFill));
    pSnapshotToFill->nStructureSize = sizeof(*pSnapshotToFill);

    /* get the library open record */
    bool bRetVal = false;
    s_mutex->API->Lock(s_mutex);
    struct LoadedLibraryRecord * pRecord = LookupLibLoadedRecordByName(pLibraryName);
    if (pRecord && (pRecord->openState == kLTLibraryManager_OpenStateOpened)) {
        LTLibraryManager_FillSnapshotInternal(pRecord, pLibraryName, pSnapshotToFill);
        bRetVal = true;
    }
    s_mutex->API->Unlock(s_mutex);
    return bRetVal;
}

void
LTLibraryManager_SnapshotOpenLibraries(LTCore_LibrarySnapshotCallbackProc * pCallbackProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    // SnapshotClientData now on the heap, for great stack justice
    SnapshotClientData *pCD = core_malloc(sizeof(SnapshotClientData));
    if (pCD) {
        // setup pCD; don't zero pCD->snapshot, that's done prior to each fill
        pCD->pCallback = pCallbackProc;
        pCD->pClientData = pClientData;

        // enumerate open libraries
        s_mutex->API->Lock(s_mutex);
        s_loadedLibraries->API->Enumerate(s_loadedLibraries, &LTLibraryManager_SnapshotOpenLibrariesEnumProc, pCD);
        s_mutex->API->Unlock(s_mutex);

        // add LTCore
        LTStdlibImpl_memset(&pCD->snapshot, 0, sizeof(pCD->snapshot));
        pCD->snapshot.nStructureSize = sizeof(pCD->snapshot);
        LTLibraryManager_FillSnapshotWithLTCore(&pCD->snapshot);
        pCallbackProc(&pCD->snapshot, pClientData);

        core_free(pCD);
    }
}

#ifdef LT_NO_DYNAMIC_LOADER
bool
LTLibraryManager_EnumerateInstalledLibraries(LTCore_InstalledLibrariesEnumProc * pEnumProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
    LTStaticallyBoundLibraryEntry * pCurr = g_pStaticallyBoundLTLibraryEntries;
    while (pCurr) {
        if (false == (*pEnumProc)(pCurr->pLibName, pClientData)) return false;
        pCurr = pCurr->pNextEntry;
    }
    return true;
}
#else
    bool
    LTLibraryManager_EnumerateInstalledLibraries(LTCore_InstalledLibrariesEnumProc * pEnumProc, void * pClientData) {
    LTCOREIMPL_ASSERT_INSIDE_THREADCONTEXT();
        return s_pBSP->hostAPI->LibraryEnumerate(pEnumProc, pClientData);
    }

    void
    LTLibraryManager_ReportLibraryLoaderFunctionFailure(const char * pFunctionName, const char * pSalientArgument, const char * pError) {
        if (! (pError && *pError)) pError = "lib load failure";
        if (NULL == pSalientArgument) pSalientArgument = "";
        if (pFunctionName && *pFunctionName) {
            LTLOG("ltcorebsp", "%s(%s): %s", pFunctionName, pSalientArgument, pError);
        }
        else {
            LTLOG("ltcorebsp", "%s", pError);
        }
    }
#endif


/******************************************************************************
 *  LOG
 ******************************************************************************
 *  13-Jul-19   augustus    created to isolate platform independent portions
 *                          of library management
 *  22-Jul-19   augustus    replaced genesis libraries with singular genesis lib name
 *  30-Oct-19   augustus    instrumented loading/unloading with console print
 *  05-Aug-20   augustus    Logger is now an LTHandle not an LTObject
 *  03-Sep-20   caligula    use LTHashTable instead of LTArray
 *  13-Sep-20   augustus    use  for record members; OpenLibrary's LibLoadedRecord now static
 *                          for stack reduction bytes: 120->56 on 32bit ARM; 176->64 on x86_64
 *  23-Jan-21   augustus    added SnapshotOpenLibraries and GetLibrarySnapshot
 *  25-Jan-21   augustus    added EnumerateInstalledLibraries
 *  16-Aug-21   augustus    use GetDriverLibraryExtrinsicName() as lookup name in CloseLibrary() for driver libs
 *  20-Aug-21   augustus    added threaded lib open (each library gets its own thread for LibInit
 *  16-Dec-21   augustus    don't let a lib open/close thread be the highest priority
 *  23-Dec-21   augustus    added GetLibraryBuildVersionString
 *  04-Aug-23   augustus    changed the library load contention timeout from 160ms to ~38 days
 */
