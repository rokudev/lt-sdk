/*******************************************************************************
**
** ExampleCoreEvent -- Simple example of using an LT Event
**
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*******************************************************************************/

#include <lt/LT.h>

#define P LT_GetCore()->ConsolePrint

static struct Statics {
    LTCore      *iCore;
    ILTThread   *iThread;
    ILTEvent    *iEvent;
    LTEvent      myEvent;
} S;

/*******************************************************************************
** This section defines an event handler function prototype and how its
** arguments will be marshalled when the event occurs.
*******************************************************************************/
typedef void (EventProc)(u32 value, char *str, void *clientData);

static const LTArgsDescriptor MyEventArgs = {2, { kLTArgType_u32, kLTArgType_charstar }};

static void DispatchMyEvent(LTEvent event, void *proc, LTArgs *args, void *clientData) {
    LT_UNUSED(event);
    (*(EventProc *)proc)(LTArgs_u32At(0, args), LTArgs_charstarAt(1, args), clientData);
}

/*******************************************************************************
** Here's the actual event function that gets called when the event occurs.
** This function definition must match the EventProc typedef above.
** Note that this function is called within the same thread context as
** where it was registered.
*******************************************************************************/
static void MyEventProc(u32 value, char *str, void *clientData) {
    LT_UNUSED(clientData);
    P("Event happened! value: %lu str: %s\n", LT_Pu32(value), str);
    if (value == 1234) {
        // Let's notify another event from within the event handler!
        S.iEvent->NotifyEvent(S.myEvent, 4321, "good-bye!");
    } else {
        // We are done. Terminate the thread.
        S.iThread->Terminate(S.iThread->GetCurrentThread());
    }
}

/*******************************************************************************
** Create an event, register the event proc, then notify the event.
** Normally, these three functions would occur in different parts of
** your library implementation, but for this example we'll do them together.
** Search the LT source to see real usage examples.
*******************************************************************************/
static bool OnThreadStart(void) {
    P("Thread starting\n");
    S.myEvent = S.iCore->CreateEvent(&MyEventArgs, DispatchMyEvent, NULL, NULL, NULL);
    S.iEvent->RegisterForEvent(S.myEvent, MyEventProc, NULL, NULL, false);
    S.iEvent->NotifyEvent(S.myEvent, 1234, "hello!");
    return true;
}

/*******************************************************************************
** When terminated, unregister the event handler and destroy the event.
*******************************************************************************/
static void OnThreadExit(void) {
    P("Thread exiting\n");
    S.iEvent->UnregisterFromEvent(S.myEvent, MyEventProc);
    S.iEvent->Destroy(S.myEvent);
}

static int ExampleCoreEvent_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    S = (struct Statics) { // clears all other fields
        .iCore   = LT_GetCore(),
        .iThread = lt_getlibraryinterface(ILTThread, LT_GetCore()),
        .iEvent  = lt_getlibraryinterface(ILTEvent,  LT_GetCore())
    };
    LTThread thread = S.iCore->CreateThread("ExampleEvent");
    if (!thread) return -1;
    S.iThread->Start(thread, OnThreadStart, OnThreadExit);
    S.iThread->WaitUntilFinished(thread, LTTime_Seconds(5));
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleCoreEvent, 1, 1024);
