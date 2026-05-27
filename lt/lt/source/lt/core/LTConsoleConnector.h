/******************************************************************************
 * lt/source/core/LTConsoleConnector.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTCONSOLECONNECTOR_H
#define ROKU_LT_SOURCE_LT_CORE_LTCONSOLECONNECTOR_H

#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTCoreBSP.h>

/**********************************************************
 * LTConsoleConnector private functions for internal LTCore use only
 **********************************************************/
void LTConsoleConnector_Init(const LTCoreBSP *pBSP);
void LTConsoleConnector_Fini(void);

/***************************************************************************
 * LTCore library public root interface functions implemented by LTConsoleConnector
 ***************************************************************************/
void LTConsoleConnector_ConsoleStompChar(char ch)                           LT_ISR_SAFE;
void LTConsoleConnector_ConsoleStompChars(const char * pChars, u32 nChars)  LT_ISR_SAFE;

/*********************************************************************
LTConsoleConnectorImpl - concrete implementation of api LTConsoleConnector
**********************************************************************/
/*
 * LTConsoleConnectorImpl private object instance data
 */
typedef_LTObjectImpl(LTConsoleConnector, LTConsoleConnectorImpl) {
    LTConsoleConnector_PutCharProc * pPutCharProc;
    void * pClientData;
    u32    connectionCount;
} LTOBJECT_API;

#endif /* #ifndef ROKU_LT_SOURCE_LT_CORE_LTCONSOLECONNECTOR_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-May-24   augustus    created
 */
