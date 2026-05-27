/*************************************************************************
 * lt/source/lt/core/LTConsoleConnector.c                                        '
 *                                                                       '
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *                                                                       '
 * _______                                                               '
 * CAUTION: No action taken in this file, neither by the procedures      '
 *          herein nor any they call, shall cause any console output     '
 *          to occur.  This means no logging, console printing, console  '
 *          stomping, or asserting in the chains of function calls that  '
 *          deploy here.  Failing to heed this directive will result     '
 *          in infinite loops, crashing, and disciplinary action.        '
 *          _____________________________________________________________'
 *          596F752068617665206E6F206368616E636520746F20737572766976652C '
 *                                      206D616B6520796F75722074696D652E '
 *************************************************************************/

#include "LTConsoleConnector.h"
#include "LTCoreImpl.h"

/***************************************
 * file LTConsoleConnector.c static variables */
static const LTCoreBSP                  * s_pBSP              = NULL;
static LTConsoleConnectorImpl    * s_pAuxiliaryConsoleConnector  = NULL;
static LTAtomic                           s_atomicConnectionCount      = { 1 };

 /*****************************************************************
  * LTCore public interface functions implemented by LTConsoleConnectorImpl *
  *****************************************************************/
void
LTConsoleConnector_ConsoleStompChar(char ch) LT_ISR_SAFE {
    LTConsoleConnector_ConsoleStompChars(&ch, 1);
}

void LT_ISR_SAFE
LTConsoleConnector_ConsoleStompChars(const char * pChars, u32 nChars) {
    /* ensure non-null s_pBSP to avoid crashing before pre-LTCore init logging is enabled */
    if (s_pBSP) {
        LTConsoleConnectorImpl * pAuxiliaryConsoleConnector = NULL;
        /* do a quick check for even takeover atom value before disabling interrupts */
        if (0 == (LTAtomic_Load(&s_atomicConnectionCount) & 1)) {
            LT_SIZE nMask = LTKDisableInterrupts();
            pAuxiliaryConsoleConnector = s_pAuxiliaryConsoleConnector;
            if (pAuxiliaryConsoleConnector && pAuxiliaryConsoleConnector->connectionCount == LTAtomic_Load(&s_atomicConnectionCount)) {
                ltobject_addref(pAuxiliaryConsoleConnector); /* make sure it doesn't go away on us when we enable interrupts and call its callback */
            }
            else {
                pAuxiliaryConsoleConnector = NULL;
            }
            LTKEnableInterrupts(nMask);
        }
        s_pBSP->PutCharsToConsole(pChars, nChars);
        if (pAuxiliaryConsoleConnector) {
            (*pAuxiliaryConsoleConnector->pPutCharProc)(pChars, nChars, pAuxiliaryConsoleConnector->pClientData);
            ltobject_removeref(pAuxiliaryConsoleConnector);
        }
    }
}

/******************************
 * LTConsoleConnector Initialization */
void
LTConsoleConnector_Init(const LTCoreBSP *pBSP) {
    s_pBSP = pBSP;
}

void
LTConsoleConnector_Fini(void) {
    LTAtomic_Store(&s_atomicConnectionCount, 1);
    s_pAuxiliaryConsoleConnector = NULL;
    s_pBSP = NULL;
}

/**************************
 * LTConsoleConnectorImpl object */
static bool
LTConsoleConnectorImpl_ConstructObject(LTConsoleConnectorImpl *thisConsole) {
    thisConsole->pPutCharProc = NULL;
    thisConsole->pClientData = NULL;
    thisConsole->connectionCount = 0;
    return true;
}

static void
LTConsoleConnectorImpl_DestructObject(LTConsoleConnectorImpl * pConsoleConnector) {
    /* Nothing to do because the object can only destruct when we release our ref count on it which
       means the object has already disconnected (if it ever connected) */
       LT_UNUSED(pConsoleConnector);
}

static bool
LTConsoleConnectorImpl_ConnectConsole(LTConsoleConnectorImpl * pConsoleConnector, LTConsoleConnector_PutCharProc *pPutCharProc, void *pClientData) {
    if (pConsoleConnector == NULL || pPutCharProc == NULL) return false; /* validate parameters  - must have a putchar proc, client data optional */
    if (pConsoleConnector->connectionCount != 0) return false;             /* validate we haven't already connected  */
    u32 connectionCount = LTAtomic_Load(&s_atomicConnectionCount);
    if (connectionCount & 0x1) {
        // odd connectionCount means a connection is available; even connectionCount means that someone is connected;  the connectionCount is odd, so try and take it */
        if (LTAtomic_CompareAndExchange(&s_atomicConnectionCount, connectionCount, connectionCount+1)) {
            // now s_atomicConnectionCount is even - we've just connected it, no one else can set up an auxiliary connection.  Further,
            // until we set our internal connectionCount to match, the setup is not finalized
            ltobject_addref(pConsoleConnector); /* make sure pConsoleConnector won't be destroyed while we have it's pointer cached */
            s_pAuxiliaryConsoleConnector = pConsoleConnector;
            pConsoleConnector->pPutCharProc  = pPutCharProc;
            pConsoleConnector->pClientData   = pClientData;
            pConsoleConnector->connectionCount = connectionCount + 1;
            return true;
        }
    }
    return false;
}

static void
LTConsoleConnectorImpl_DisconnectConsole(LTConsoleConnectorImpl * pConsoleConnector) {
    if (pConsoleConnector->connectionCount) {
        u32 connectionCount = LTAtomic_Load(&s_atomicConnectionCount);
        if (pConsoleConnector->connectionCount == connectionCount) {
            /* I have the console, must relinquish it; first invalidate myself, then the connection variables */
            pConsoleConnector->connectionCount = 0;
            s_pAuxiliaryConsoleConnector = NULL;
            pConsoleConnector->pPutCharProc  = NULL;
            pConsoleConnector->pClientData   = NULL;
            ltobject_removeref(pConsoleConnector);
            LTAtomic_Store(&s_atomicConnectionCount, connectionCount + 1); /* always set s_atomicConnectionCount last; this makes it an odd number, indicating available to takeover */
        }
    }
}

static void
LTConsoleConnectorImpl_SubmitConsoleInput(LTConsoleConnectorImpl *pConsoleConnector, const char *pChars, u32 numChars) LT_ISR_SAFE {
    if (pConsoleConnector->connectionCount && pConsoleConnector->connectionCount == LTAtomic_Load(&s_atomicConnectionCount)) {
       /* only submit console input if we are the takeover console */
        LT_SIZE nMask = LTKDisableInterrupts();
        LTCoreImpl_ProcessISRConsoleInputChars(pChars, numChars);
        LTKEnableInterrupts(nMask);
    }
}

define_LTObjectImplPublic(LTConsoleConnector, LTConsoleConnectorImpl,
    ConnectConsole,
    DisconnectConsole,
    SubmitConsoleInput);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-May-24   augustus    created
 */
