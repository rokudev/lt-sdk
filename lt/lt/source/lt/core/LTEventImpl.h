/******************************************************************************
 * lt/source/core/LTEventImpl.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTEVENTIMPL_H
#define ROKU_LT_SOURCE_LT_CORE_LTEVENTIMPL_H

#include <lt/core/LTEvent.h>

/**********************************************
 * LTEventImpl_Init initialization functions */
void    LTEventImpl_Init(void);
void    LTEventImpl_Fini(void);

/***************************************
 * LTCore public interface functions */
LTEvent LTEventImpl_CreateEvent(const LTArgsDescriptor * pEventArgsDescriptor, LTEvent_DispatchProc * pEventDispatchProc, LTEvent_DispatchCompleteProc * pEventDispatchCompleteProc, LTEvent_NotifyImmediateEventStateProc * pNotifyImmediateEventStateProc, void * pNotifyImmediateEventStateClientData);
void    LTEventImpl_RegisterForEvent(LTEvent hEvent, void * pReceiverEventProc, LTThread_ClientDataReleaseProc * pReceiverClientDataReleaseProc, void * pReceiverClientData, bool bNotifyEventStateImmediately);
bool    LTEventImpl_UnregisterFromEvent(LTEvent hEvent, void * pReceiverEventProc);
void    LTEventImpl_UnregisterFromCurrentEvent(void);
void    LTEventImpl_NotifyEvent(LTEvent hEvent, ...);

#endif // #ifndef ROKU_LT_SOURCE_LT_CORE_LTEVENTIMPL_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Sep-20   augustus    created
 *  22-Jul-21   augustus    make functions available for LTCore lib internal direct access
*/
