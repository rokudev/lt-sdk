/*******************************************************************************
 *
 * LTShellPins: Pins Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/system/shell/LTSystemShell.h>

// DEFINE_LTLOG_SECTION("ltshell.pins");

/** Standard LT Interfaces ****************************************************/
static LTDevicePins *s_DevPins;
static ILTShell * SHL_iShell;

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/

static LTSystemShell *SHL_Library;

/** Shell Variables ***********************************************************/

typedef ILTDriverPins_BidirectionalBank IPinBank;

typedef struct {
    LTList_Node node;
    LTDeviceUnit hPin;
    LTShell hShell;
} HandleNode;

static LTList IRQ1Handles; // Pin handles for IRQs calling irq1 callback
static LTList IRQ2Handles; // Pin handles for IRQs calling irq2 callback

/** Forward references ********************************************************/

static int  SHL_Info(LTShell hShell, int argc, const char *argv[]);
static void testirq1(bool val, void* data);
static void testirq2(bool val, void* data);

/** Utility Functions *********************************************************/

static LTDeviceUnit GetPinByName(const char *bankname) {
    u32             nDeviceUnitIndex    = 0;
    if (s_DevPins->GetUnitNumberFromBankName(bankname, &nDeviceUnitIndex)) {
        return s_DevPins->CreateDeviceUnitHandle(nDeviceUnitIndex);
    }
    return 0;
}

static int SetIRQ(LTShell hShell, int irq, const char *bankname) {
    LTDeviceUnit    hPin              = GetPinByName(bankname);
    if (hPin) {
        IPinBank *iBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin);
        if (iBank) {
            HandleNode *hn = lt_malloc(sizeof(HandleNode));
            if (hn) {
                SHL_iShell->Print(hShell, "Setting IRQ%u on GPIO %s Handle %u\n", irq+1, bankname, (unsigned int) hPin);
                iBank->EnableIRQ(hPin, kLTDevicePin_PinConfiguration_Trigger_BothEdges, LTTime_Milliseconds(0), (irq)?testirq2:testirq1, (void *)hn);
                hn->hPin = hPin;
                hn->hShell = hShell;
                LTList_AddTail(&IRQ1Handles, &(hn->node));
                return 0;
            } else {
                SHL_iShell->Print(hShell, "Failed to create IRQ, OOM \n");
            }
        } else {
            SHL_iShell->Print(hShell, "Failed to obtain and configure unit handle for %s\n", bankname);
        }
        // Failure if this is reached
        LT_GetCore()->DestroyHandle(hPin);
    }
    return -1;
}


/** Callback Functions ********************************************************/

static void testirq1(bool val, void* data) {
    HandleNode *hn = (HandleNode *)data;
    SHL_iShell->Print(hn->hShell, "testirq1: Pin is %u, Pin handle %u\n", val, (unsigned int)hn->hPin);
}

static void testirq2(bool val, void* data) {
    HandleNode *hn = (HandleNode *)data;
    SHL_iShell->Print(hn->hShell, "testirq2: Pin is %u, Pin handle %u\n", val, (unsigned int)hn->hPin);
}

/** Command Functions *********************************************************/

static int SHL_Info(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    u32                                 nDeviceUnits    = s_DevPins->GetNumDeviceUnits();
    ILTDriverPins_BidirectionalBank *   iBank           = NULL;

    for (u32 nDevice = 0; nDevice < nDeviceUnits; ++nDevice) {
        char const * pBankName = NULL;
        LTDeviceUnit hPin = s_DevPins->CreateDeviceUnitHandle(nDevice);
        s_DevPins->GetBankNameFromUnitNumber(nDevice, &pBankName);
        iBank = hPin ? lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin) : NULL;
        if (!iBank || !pBankName) { SHL_iShell->Print(hShell, "Error getting pin %u\n", (unsigned int)nDevice); continue; }
        u32 value = iBank->Read(hPin);
        SHL_iShell->Print(hShell, "Pin %u:\t %s \t\t Value: %u\n", (unsigned int)nDevice, pBankName, (unsigned int)value);
        LT_GetCore()->DestroyHandle(hPin);
    }
    return 0;
}

static int SHL_Get(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: getpin pinname\n");
    }

    LTDeviceUnit    hPin   = GetPinByName(argv[1]);
    IPinBank         *iBank = hPin ? lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin) : NULL;
    if (iBank) {
        SHL_iShell->Print(hShell, "GPIO %s is %u\n", argv[1], (unsigned int)iBank->Read(hPin));
    } else {
        SHL_iShell->Print(hShell, "Failed to obtain interface for %s\n", argv[1]);
    }
    LT_GetCore()->DestroyHandle(hPin);
    return 0;
}

static int SHL_Set(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: set npinname 1|0\n");
    }

    LTDeviceUnit    hPin   = GetPinByName(argv[1]);
    IPinBank        *iBank = hPin ? lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin) : NULL;
    if (iBank) {
        u32 value = lt_strtou32(argv[2], NULL, 10);
        SHL_iShell->Print(hShell, "Setting GPIO %s to %u\n", argv[1], (unsigned int)value);
        iBank->Set(hPin, value);
    } else {
        SHL_iShell->Print(hShell, "Failed to obtain and configure unit handle for %s\n", argv[1]);
    }
    LT_GetCore()->DestroyHandle(hPin);
    return 0;
}

static int SHL_SetDir(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: setpindir npinname 1|0\n");
    }

    LTDeviceUnit    hPin   = GetPinByName(argv[1]);
    IPinBank        *iBank = hPin ? lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin) : NULL;
    if (iBank) {
        u32 direction = lt_strtou32(argv[2], NULL, 10);
        SHL_iShell->Print(hShell, "Setting GPIO %s direction to %s\n", argv[1], direction?"in":"out");
        if (direction) {
            iBank->ConfigureAsInput(hPin, kLTDevicePin_PinConfiguration_PullType_NoPull);
        } else {
            iBank->ConfigureAsOutput(hPin, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        }
    } else {
        SHL_iShell->Print(hShell, "Failed to obtain and configure unit handle for %s\n", argv[1]);
    }
    LT_GetCore()->DestroyHandle(hPin);
    return 0;
}

static int SHL_SetIRQ1(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: setirq1 npinname 1|0\n");
    }

    return SetIRQ(hShell, 0, argv[1]);
}

static int SHL_SetIRQ2(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2) {
        SHL_iShell->Print(hShell, "usage: setirq2 npinname 1|0\n");
    }

    return SetIRQ(hShell, 1, argv[1]);
}

static int SHL_ClearIRQ(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);
    LT_UNUSED(argc);

    if (hShell) SHL_iShell->Print(hShell, "Clearing irqs\n");

    LTList_ForEach(node, &IRQ1Handles) {
        HandleNode *hn = (HandleNode *) node;
        LT_GetCore()->DestroyHandle(hn->hPin);
        lt_free(hn);
    } LTList_EndForEach;
    LTList_Init(&IRQ1Handles);

    LTList_ForEach(node, &IRQ2Handles) {
        HandleNode *hn = (HandleNode *) node;
        LT_GetCore()->DestroyHandle(hn->hPin);
        lt_free(hn);
    } LTList_EndForEach;
    LTList_Init(&IRQ2Handles);

    return 0;
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "listpins",     SHL_Info,      "List available pins",  NULL },
    { "getpin",       SHL_Get,       "Get pin value",        NULL },
    { "setpin",       SHL_Set,       "Set pin value",        NULL },
    { "setpindir",    SHL_SetDir,    "Set pin direction",    NULL },
    { "setirq1",      SHL_SetIRQ1,   "Set IRQ1 on pin",      NULL },
    { "setirq2",      SHL_SetIRQ2,   "Set IRQ2 on pin",      NULL },
    { "clearirq",     SHL_ClearIRQ,  "Clear IRQ on pin",     NULL },
};

static void SHL_Quit(void) {
    if (SHL_Library) {
        SHL_ClearIRQ(0, 0, NULL);
        SHL_Library->UnregisterCommands(SHL_Commands);
        lt_closelibrary(SHL_Library);
    }
}

static bool SHL_Init(void) {
    SHL_Library = lt_openlibrary(LTSystemShell);
    if (!SHL_Library) return false;

    SHL_iShell = lt_getlibraryinterface(ILTShell, SHL_Library);
    SHL_Library->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    return true;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/

static void LTShellPinsImpl_LibFini(void) {
    SHL_Quit();
    lt_closelibrary(s_DevPins);       // null ok
}

static bool LTShellPinsImpl_LibInit(void) {
    LTList_Init(&IRQ1Handles); // Initialize Handles list for IRQ1
    LTList_Init(&IRQ2Handles); // Initialize Handles list for IRQ2
    s_DevPins = lt_openlibrary(LTDevicePins);
    if (s_DevPins && SHL_Init()) return true;
    LTShellPinsImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellPins, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellPins) LTLIBRARY_DEFINITION;
