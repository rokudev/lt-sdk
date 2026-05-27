/******************************************************************************
 * <lt/core/LTEvent.h>                         LT Event - asynchronous notifier
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTEVENT_H
#define ROKU_LT_INCLUDE_LT_CORE_LTEVENT_H

#include <lt/core/LTThread.h>
LT_EXTERN_C_BEGIN

/**
 * @defgroup ltcore_event LTEvent
 * @ingroup ltcore
 * @{
 */

/*____________________
 / LTEvent TYPEDEFS */
typedef LTHandle LTEvent;
typedef void (LTEvent_DispatchProc)(LTEvent hEvent, void * pEventProc, LTArgs * pEventArgs, void * pEventProcClientData);
    /**< procedure supplied to CreateEvent() function that is called by event subsystem in event receiver thread context for invocation of receiver's typed pEventProc using pEventArgs and pEventProcClientData
     * When an event is created with CreateEvent() the creator supplies an LTEvent_DispatchProc.  The job of the LTEvent_DispatchProc is to invoke the receiver's pEventProc using the
     * arguments in pEventArgs for the pEventProc parameters
     * @param hEvent the event being notified
     * @param pEventProc The address of the event receiver's event proc to cast to the event callback type and invoke with the event args in pEventArgs
     * @param pEventArgs pointer to an LTArgs struct that contains the event arguments to pass into pEventProc
     * @param pEventProcClientData The receiver's opaque client data pointer that was passed in to RegisterForEvent() when the the event receiver was registered
     *
     */

typedef void (LTEvent_NotifyImmediateEventStateProc)(LTEvent hEvent, void * pNotifyImmediateEventStateClientData, void * pEventProc, void * pEventProcClientData);
    /**< procedure supplied to CreateEvent() function called by event subsystem to synchronously notify receiver of event state on registration
     *
     * When an event receiver registers for an event, they may specify true for bNotifyEventStateImmediately.  In this case
     * the LTEvent subsystem will invoke the event creator's LTEvent_NotifyImmediateEventStateProc.
     * @param hEvent the event for which synchronous state delivery is requested
     * @param pNotifyImmediateEventStateClientData The event creator's opaque data pointer that was passed in to CreateEvent() when the event was created
     * @param pEventProc The address of the event receiver's event proc to cast to the event callback type and invoke directly with parameters describing the current state
     * @param pEventProcClientData The receiver's opaque client data pointer that was passed in to RegisterForEvent() when the the event receiver was registered
     * @note NULL may be passed in to CreateEvent for the LTEvent_NotifyImmediateEventStateProc parameter in which case no synchronous notification of event
     *       state upon registration will be available and the value of bNotifyEventStateImmediately passed in by a receiver when registering for the event will be ignored.
     */

typedef void (LTEvent_DispatchCompleteProc)(LTEvent hEvent, LTArgs * pEventArgs);
    /**< procedure supplied to CreateEvent() that, if non-null, is called by event subsystem in event notifier thread context to allow pEventArgs specific cleanup
     * When an event is createdwith CreateEvent() the creator supplies an optional LTEvent_DispatchCompleteProc or NULL if no LTEvent_DispatchCompleteProc is
     * required or desired.
     * The job of the LTEvent_DispatchCompleteProc is to clean up any event notification specific data that was passed as an argument to NotifyEvent.  Such arguments
     * are passed to the LTEvent_DispatchCompleteProc in the pEventArgs structure.
     * @param hEvent the event whose notification just completed
     * @param pEventArgs the arguments that were passed in as varargs to NotifyEvent().
     *
     */

typedef void (LTEvent_ISRThreadProxyNotifyProc)(void *pClientData);
    /**< thread proxy notify procedure supplied to %NotifyEventFromISR() that will be called from a system proxy thread to Notify the event
     *
     * @param pClientData client data containing the hEvent and event arguments the thread proxy notify procedure should use to call %NotifyEvent
     *
     */
/*_____________________
 / ILTEvent INTERFACE /
 */
TYPEDEF_LTLIBRARY_INTERFACE(ILTEvent, 1);

struct ILTEventApi {

    INHERIT_INTERFACE_BASE

    void   (* RegisterForEvent)(LTEvent hEvent, void * pReceiverEventProc, LTThread_ClientDataReleaseProc * pReceiverClientDataReleaseProc, void * pReceiverClientData, bool bNotifyEventStateImmediately);
    /**< registers an event receiver callback for notification of event in the current thread context
     *   RegisterForEvent() is called to register for receipt of event notification.  Note that the type of the pReceiverEventProc
     *   is a void pointer, generic to the LTEvent implementation.  This is done so the event can use an arbitrary event receiver
     *   procedure type (one per LTEvent instance). See note below.
     *   @param hEvent the event to register with
     *   @param pReceiverEventProc the event receiver proc that will be called with direct arguments in the context of the receiver's thread
     *   @param pReceiverClientDataReleaseProc the release proc to call when the event is unregistered or when the LTEvent is destroyed with clients still registered
     *   @param pReceiverClientData an opaque pointer to pass through to pReceiverEventProc
     *   @param bNotifyEventStateImmediately whether or not the client wants immediate notification of the current state of the event
     *   @note If bNotifyEventStateImmediately is true then the current state of the event is delivered to the client synchronously, after the registration takes place,
     *         immediately prior to this procedure returning.
     *         The 'current state of the event' is event specific but usually is a notification using the same event data as the most recent previous notification)
     *   @note <br>
     *   <br>
     *   Event receivers typically don't call RegisterForEvent directly, rather they typically call a strongly typed function declared in
     *   the public interface of the library that created and notifies the event.<br>
     *   For example, LTSystemNetwork.h has:<pre>
     *   typedef enum { kLTNetwork_ResultOk, kLTNetwork_ResultNotImplemented, kLTNetwork_ResultBadSocket, (more enum values) } LTNetworkResult;
     *   typedef enum { kLTNetwork_EventNone, kLTNetwork_EventReady, kLTNetwork_EventError, (more enum values) } LTNetworkEvent;
     *   typedef void (LTSystemNetwork_OnNetworkSocketEventProc)(LTSocket hSocket, LTNetworkEvent event, LTNetworkResult result, void * pClientData);
     *   typedef_LTLIBRARY_ROOT_INTERFACE(LTSystemNetwork, 1) {
     *       ... (functions)
     *       void (* WhenNetworkSocketEvent)(LTSocket hSocket, LTSystemNetwork_OnNetworkSocketEventProc * pOnNetworkSocketEventProc, void * pClientData);
     *       ... (more functions)
     *   } LTLIBRARY_INTERFACE;</pre>
     *   LTSystemNetworkimpl.c has:<pre>
     *   void LTNetworkImpl_WhenNetworkSocketEvent(LTSocket hSocket, LTSystemNetwork_OnNetworkSocketEventProc * pOnNetworkSocketEventProc, void * pClientData) {
     *       LTEvent hSocketEvent = LTSystemNetworkImpl_SocketEventFromSocket(hSocket);                                                                                                       // LOOK!
     *       if (hSocketEvent) iEvent->RegisterForEvent(hSocketEvent, pOnNetworkSocketEventProc, pClientData);  // this is where RegisterForEvent is called
     *   }
     *   define_LTLIBRARY_ROOT_INTERFACE(LTSystemNetwork, 1) {
     *       ... (functions)
     *       void (* WhenNetworkSocketEvent)(LTSocket hSocket, LSystemNetwork_OnNetworkSocketEventProc * pOnNetworkSocketEventProc, void * pClientData); // sync
     *       ... (more functions)
     *   } LTLIBRARY_DEFINITION;</pre>
     *   And the client would register like this:<pre>
     *   typedef struct MySocketEventClientData { u32 nNumTotalSocketErrors; (more members) };
     *   static void MyOnNetworkSocketEvent(LTSocket hSocket, LTNetworkEvent event, LTNetworkResult result, void * pClientData) {
     *       ifdef LT_DEBUG
     *            LT_GetCore()->ConsolePrint("MyOnNetworkSocketEvent received event %s with result %s\n", iLTSystemNetwork->EventToString(event), iLTSystemNetwork->ResultToString(result));
     *       endif
     *       if (kLTNetwork_EventError == event) {
     *           MySocketEventClientData * pMyClientData = (MySocketEventClientData *)pClientData;
     *           pMyClientData->nNumTotalSocketErrors++;
     *       }
     *   }
     *   pLTSystemNetwork->WhenNetworkSocketEvent(&MyOnNetworkSocket, pMyClientData);</pre>
     *     *
     *   */

    bool   (* UnregisterFromEvent)(LTEvent hEvent, void * pReceiverEventProc);
    /**< unregisters the current thread's pReceiverEventProc from hEvent
     *   UnregisterFromEvent() is called to unregister the current thread's previously registered pReceiverEventProc
     *   @param hEvent the event to unregister from
     *   @param pReceiverEventProc the event receiver proc to unregister
     *   @return true if pReceiverEventProc was registered by the calling thread and was unregistered, false otherwise */

    void   (* RegisterThreadForEvent)(LTEvent hEvent, LTThread hReceiverThread, void * pReceiverEventProc, LTThread_ClientDataReleaseProc * pReceiverClientDataReleaseProc, void * pReceiverClientData, bool bNotifyEventStateImmediately);
    /**< registers an event receiver callback for notification of event in the thread context of a specific thread
     *   RegisterThreadForEvent() is called to register a specific thread for receipt of event notification.
     *   @param hEvent the event to register with
     *   @param hReceiverThread the receiver thread to register in whose thread context pReceiverEventProc will be called
     *   @param pReceiverEventProc the event receiver proc that will be called with direct arguments in the context of hReceiverThread
     *   @param pReceiverClientDataReleaseProc the release proc to call when the event is unregistered or when the LTEvent is destroyed with clients still registered
     *   @param pReceiverClientData an opaque pointer to pass through to pReceiverEventProc
     *   @param bNotifyEventStateImmediately whether or not the client wants immediate notification of the current state of the event
     *   @note If bNotifyEventStateImmediately is true then the current state of the event is delivered to the client synchronously, after the registration takes place,
     *         immediately prior to this procedure returning.
     *         The 'current state of the event' is event specific but usually is a notification using the same event data as the most recent previous notification)
     *   @see RegisterForEvent, UnregisterThreadFromEvent */

    bool   (* UnregisterThreadFromEvent)(LTEvent hEvent, LTThread hReceiverThread, void * pReceiverEventProc);
   /**< unregisters a specific thread's pReceiverEventProc from hEvent
    *   UnregisterThreadFromEvent() is called to unregister a specific thread's previously registered pReceiverEventProc
    *   @param hEvent the event to unregister from
    *   @param hReceiverThread the receiver thread to unregister
    *   @param pReceiverEventProc the receiver thread's event receiver proc to unregister
    *   @return true if pReceiverEventProc was registered by the calling thread and was unregistered, false otherwise
    *   @see RegisterThreadForEvent */

    void   (* UnregisterFromCurrentEvent)(void);
   /**< unregisters the currently executing pReceiverEventProc from its hEvent that is being notified
    *   UnregisterFromCurrentEvent() may only be called from an event receiver event proc and will unregister the current
    *   thread/event proc from the event being notified.  The unregistration takes place immediately after the event receiver
    *   proc returns.  If another instance of the same event is notified after UnregisterFromCurrentEvent() is called but before
    *   the event receiver proc returns, the new event notification will be delivered.
    *   For instant fidelity of event unregistration, use UnregisterFromEvent() or just call UnregisterFromCurrentEvent() immediately prior to
    *   returning from the event receiver event proc.
    */

    void   (* NotifyEvent)(LTEvent hEvent, ...);
   /**< Notifies all registered receivers of the event
    *   NotifyEvent() causes all registered event receiver callbacks to be called in the context of each corresponding event receiver thread,
    *   with the varargs event data passed in here delivered to the event receiver callback as concrete, typed values passed as arguments to the callback.
    *   The machinery of LTEvent does this by invoking the LTEvent_DispatchProc supplied to LTCore->CreateEvent(), once for each registered
    *   receiver, in the context of each event receiver's corresponding thread.  The varargs event data passed to NotifyEvent() is converted to a
    *   heap accessible LTArgs structure and thread boundary transition and then supplied to the LTEvent_DispatchProc along with the event receiver's
    *   event callback which it invokes directly, passing in LTArgs typed values for the function arguments.
    *   @param hEvent the event to notify
    *   @param ... the event data arguments given in the same order as was specified in the LTArgsDescriptor supplied to LTCore->CreateEvent()
    *   @see LTCore_CreateEvent
    */

    void   (* NotifyEventFromISR)(LTEvent_ISRThreadProxyNotifyProc *pThreadProxyNotifyProc, void *pClientData) LT_ISR_SAFE;
   /**< Schedules event notification on system proxy thread from ISR
    *   %NotifyEventFromISR causes the supplied pThreadProxyNotifyProc to be called from a system proxy thread.
    *   The supplied pThreadProxyNotifyProc should call NotifyEvent with using the hEvent and event arguments passed through clientData.
    *   @param pThreadProxyNotifyProc the thread proxy notify proc that will call %NotifyEvent
    *   @param pClientData the client data containing the hEvent and arguments to pass to %NotifyEvent
    *   @see NotifyEvent
    */

};

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTEVENT_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  24-Aug-20   augustus    created
 *  10-Sep-20   augustus    redid naming to make it clearer per Carl's suggestions;
 *                          added Doxygen example code using LTSystemNetwork events;
 *                          added Unregister functions
 *  15-Jan-21   augustus    added RegisterThreadForEvent and UnregisterThreadFromEvent
 *  30-Mar-21   augustus    added NotifyLastRegisteredReceiverOfEvent
 *  20-Oct-22   augustus    added the client data release proc so clients can clean up
 *                          client data resources on demand on unregistration or if the
 *                          event is destroyed while a client is still registered.
 *  21-Oct-22   augustus    added capability to be notified of current event state synchronously
 *                          during registration, deftly avoiding race conditions that cause
 *                          missed notifications and stale/null comprehension of state
 *  24-Jul-25   augustus    added NotifyEventFromISR
 */
