/******************************************************************************
 * source/lt/system/shell/LTSystemShellImpl.c
 *
 * implementation of LTSystemShell library interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include "LTSystemShellImpl.h"
#include "LTShellImpl.h"
#include "LTShellCommands.h"
#include "LTConsoleUSB.h"
#include <lt/core/LTCore.h>
#include <lt/device/identity/LTDeviceIdentity.h>

DEFINE_LTLOG_SECTION("ltsystemshell");

typedef struct _RegisteredCommandDescsListNode {
    struct _RegisteredCommandDescsListNode  * pNext;
    LTSystemShell_CommandDesc               * pCommandDescs;
    u32                                       nNumCommands;
} RegisteredCommandDescsListNode;

static LTCore   * s_pCore       = NULL;
static LTMutex  * s_mutex       = NULL;

static RegisteredCommandDescsListNode * s_pRegisteredCommandDescsListHead = NULL;
static RegisteredCommandDescsListNode * s_pRegisteredCommandDescsListTail = NULL;

/*______________________________________________
_/ LTSystemShellImpl private static functions */
static bool LTSystemShellImpl_LookupCommandEnumProc(const LTSystemShell_CommandDesc * pCommandDesc, void * pClientData) {
    LTSystemShell_CommandDesc * pCommandDescToSet = (LTSystemShell_CommandDesc *)pClientData;
    if (0 == lt_strcmp(pCommandDesc->pCommand, pCommandDescToSet->pCommand)) {
        *pCommandDescToSet = *pCommandDesc;
        return false; /* found it, abort enumeration */
    }
    return true;
}

/*_____________________________________________________________________
_/ LTSystemShellImpl command table access functions (library private) */
bool
LTSystemShellImpl_LookupCommand(const char * pCommand, LTSystemShell_CommandDesc * pCommandDescToSet) {
    return (pCommandDescToSet && (pCommandDescToSet->pCommand = pCommand) && *pCommand) ? (LTSystemShellImpl_EnumerateCommands(&LTSystemShellImpl_LookupCommandEnumProc, pCommandDescToSet) ? false : true) : false;
}

bool
LTSystemShellImpl_EnumerateCommands(LTSystemShellImpl_CommandEnumProc * pEnumProc, void * pClientData) {
    s_mutex->API->Lock(s_mutex);
    RegisteredCommandDescsListNode * pNode = s_pRegisteredCommandDescsListHead;
    while (pNode) {
        LTSystemShell_CommandDesc * pDesc = pNode->pCommandDescs;
        u32 nCount = pNode->nNumCommands;
        while (nCount-- && pDesc && pDesc->pCommand) {
            if (! (*pEnumProc)(pDesc, pClientData)) { s_mutex->API->Unlock(s_mutex); return false; } /* returning false aborts enumeration */
            pDesc++;
        }
        pNode = pNode->pNext;
    }
    s_mutex->API->Unlock(s_mutex);
    return true;
}

/*____________________________________________________
 / LTSystemShell library public interface functions */
void
LTSystemShellImpl_RegisterCommands(const LTSystemShell_CommandDesc commandDescs[], u32 nNumCommands) {
    if (NULL == commandDescs || nNumCommands == 0) return;
    s_mutex->API->Lock(s_mutex);
    /* make sure the commandDescs struct isn't already registered*/
    RegisteredCommandDescsListNode * pNode = s_pRegisteredCommandDescsListHead;
    while (pNode) {
        if (pNode->pCommandDescs == commandDescs) {
            s_mutex->API->Unlock(s_mutex);
            return;
        }
        pNode = pNode->pNext;
    }
    /* make a new node containing commandDescs and add it to the tail of the list */
    pNode = lt_malloc(sizeof(*pNode));
    pNode->pNext = NULL;
    pNode->pCommandDescs = (LTSystemShell_CommandDesc *)commandDescs;
    pNode->nNumCommands = nNumCommands;
    if (s_pRegisteredCommandDescsListTail) {
        s_pRegisteredCommandDescsListTail->pNext = pNode;
        s_pRegisteredCommandDescsListTail = pNode;
    }
    else s_pRegisteredCommandDescsListHead = s_pRegisteredCommandDescsListTail = pNode;
    s_mutex->API->Unlock(s_mutex);
}

void
LTSystemShellImpl_UnregisterCommands(const LTSystemShell_CommandDesc commandDescs[]) {
    s_mutex->API->Lock(s_mutex);
    RegisteredCommandDescsListNode * pPrev = NULL;
    RegisteredCommandDescsListNode * pCurr = s_pRegisteredCommandDescsListHead;
    while (pCurr) {
        if (pCurr->pCommandDescs == commandDescs) {
            if (pPrev) pPrev->pNext = pCurr->pNext;
            else s_pRegisteredCommandDescsListHead = pCurr->pNext;
            if (NULL == pCurr->pNext) s_pRegisteredCommandDescsListTail = pPrev;
            lt_free(pCurr);
            break;
        }
        pPrev = pCurr;
        pCurr = pCurr->pNext;
    }
    s_mutex->API->Unlock(s_mutex);
}

/*_________________________________
_/ LibInit and LibFini functions */
static void LTSystemShellImpl_LibFini(void);
static bool LTSystemShellImpl_LibInit(void) {

    s_pCore     = LT_GetCore();

    volatile u32 load = 0x9FACB180;

    LTDeviceIdentity *deviceIdentity = lt_openlibrary(LTDeviceIdentity);

    if (deviceIdentity && deviceIdentity->IsUnsecured() != kLTSecurityStatus_Success &&
        deviceIdentity->IsManufacturingFirmware() != kLTSecurityStatus_Success) {
        /* We are running on a unit that has been secured - it has an LTDeviceIdentity library
        and that library has reported that the unit is not unsecured, i.e. it is secured.
        We can only run the shell if we have an LTAT entitlement to do so. */
        if (deviceIdentity->CheckLTATClaims(kLTSecurityClaimGroup2, kLTSecurityClaimGroup2_EnableConsoleInput) != kLTSecurityStatus_Success) {
            /* This is a secured unit that doesn't have the LTAT entitlement for running LTSystemShell; refuse loading */
            LTLOG("disabled", "Secured unit, no entitlement present, LTSystemShell disabled");
            lt_closelibrary(deviceIdentity);
            LT_GetCore()->EnableSerialConsole(false);
            return false;
        }
        LT_GetCore()->EnableSerialConsole(true);
        LTLOG("enabled.secured", "Secured unit, entitlement present, LTSystemShell enabled");
    }
    else {
        /* This unit is not secured.  Either it has no LTDeviceIdentity library which
           means it can't be secured, or it does and the LTDeviceIdentity library has
           reported the unit is unsecured.  We allow running LTSystemShell on
           unsecured units.
        */
        LT_GetCore()->EnableSerialConsole(true);
        LTLOG("enabled.unsecured", "Unsecured unit, LTSystemShell enabled");
    }
    if (deviceIdentity) lt_closelibrary(deviceIdentity);

    // create mutex
    s_mutex    = lt_createobject(LTMutex);

    // initialize LTConsoleUSB for supporting switching between UART and USB devices for the console
    LTConsoleUSB_Init();

    // init LTShellCommands; it is now managing (holding open) libraries opened with ltlib --open and needs initialization
    LTShellCommands_LibInit();

    // register the inbuilt commands
    LTSystemShellImpl_RegisterCommands(LTShellCommands_GetInbuiltShellCommands(), LTShellCommands_GetNumInbuiltShellCommands());

    // initialize LTShellImpl, this will create the console shell
    LTShellImpl_LibInit();

    if (load != 0x9FACB180) {
        LTSystemShellImpl_LibFini();
        return false;
    }

    return true;
}

static void
LTSystemShellImpl_LibFini(void) {

    // finalize LTShellImpl, this will destroy the console shell
    LTShellImpl_LibFini();

    // free all of the registered command desc list nodes
    while (s_pRegisteredCommandDescsListHead) {
        s_pRegisteredCommandDescsListTail = s_pRegisteredCommandDescsListHead;
        s_pRegisteredCommandDescsListHead = s_pRegisteredCommandDescsListHead->pNext;
        lt_free(s_pRegisteredCommandDescsListTail);
    }
    s_pRegisteredCommandDescsListTail = NULL;

    // finalize LTConsoleUSB - this will switch the console backback to the UART as required
    LTConsoleUSB_Fini();

    // finalize LTSellCommands; this will close any libraries that were opened during shell sessions
    LTShellCommands_LibFini();

    // destroy mutex
    lt_destroyobject(s_mutex);
    s_mutex  = NULL;

    // nullify the interfaces
    s_pCore = NULL;
}

/*______________________________________
_/ LTSystemShell library Run function */
static int LTSystemShellImpl_Run(int argc, const char ** argv) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    /* This run function does nothing but it has to be in place for LTSystemShell to be a genesis library or run with ltrun */
    return 0;
}

/*___________________________________________
 / LTSystemShell library interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTSystemShell, LTSystemShellImpl_Run, 512) {
    .RegisterCommands             = &LTSystemShellImpl_RegisterCommands,
    .UnregisterCommands           = &LTSystemShellImpl_UnregisterCommands,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(
    LTSystemShell,
        (ILTShell)
)

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-Nov-20   augustus    created
 *  18-Dec-20   augustus    enumeration can be aborted; LookupCommand is thread safe
 */
