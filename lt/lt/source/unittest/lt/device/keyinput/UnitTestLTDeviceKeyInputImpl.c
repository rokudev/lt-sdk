/*******************************************************************************
 * Key Input Unit Test
 *
 * Test LTDeviceKeyInput and <platform>DriverKeyInput.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/keyinput/LTDeviceKeyInput.h>

DEFINE_LTLOG_SECTION("unittest.keyinput");

/*******************************************************************************
 * Typedefs
*******************************************************************************/
typedef enum {
    kKeyInputSuccess                                = 0,
    // press failures
    kKeyInputInvalidKeyPressed                      = 10,
    kKeyInputInvalidKeyReleased                     = 11,
    kKeyInputInvalidClientData                      = 12,
    kKeyInputUnknowTest                             = 13,
    kKeyInputAnyKeyDownFailed                       = 14,
    // load failures
    kKeyInputDeviceLibraryLoadFailed                = 20,
    kKeyInputTestTheadStartFailed                   = 21
} KeyInputTestResults;

typedef enum {
    kTestKeyA,
    kTestKeyB,
    kTestKeyC,
    kTestKeyD,
    kTestKeyE,
    kTestKeyF,
    kTestKeyHold,
    kTestKeyUnstuck,
} KeyInputTestType;

typedef struct {
    KeyInputTestType        eTest;  // the test to perform
    LTDeviceKeyInputValue   nKey; // the key to test
} KeyInputTest;

/*******************************************************************************
 * Consts
*******************************************************************************/
static const LTDeviceKeyInputValue  kKeyHoldTestKey     = kLTKeyPartnerA;

static const KeyInputTest kTests[]                      = {
    {   kTestKeyA,            kLTKeyPartnerA      },
    {   kTestKeyB,            kLTKeyPartnerB      },
    {   kTestKeyC,            kLTKeyPartnerC      },
    {   kTestKeyD,            kLTKeyPartnerD      },
    {   kTestKeyE,            kLTKeyPartnerE      },
    {   kTestKeyF,            kLTKeyPartnerF      },
    {   kTestKeyHold,         kLTKeyPartnerA      },
    {   kTestKeyUnstuck,      kLTKeyPartnerA      },
};
static const u8                     kTestsCount         = sizeof(kTests) / sizeof(KeyInputTest);

// client data test value to make sure the client data is passed back
static const char                 * s_pClientDataTest   = "Client data test string";

static const u32                    kStackSize          = 1024;

static const LTTime                 kKeyHoldTime        = LTTimeInitializer_Seconds(2);

/*******************************************************************************
 * Static variables
*******************************************************************************/
static ILTThread                  * s_iThread           = NULL;
static LTDeviceKeyInput           * s_pKeyInput         = NULL;

// current test
static u8                           s_nCurrentTest      = -1;

static KeyInputTestResults          s_eResult           = kKeyInputSuccess;

/*******************************************************************************
 * Test result banners
*******************************************************************************/
static char const                 * kFailedBanner       = {
    " _________________________________________________ \n"
    "/       _________    ______    __________          \n"
    "/      / ____/   |  /  _/ /   / ____/ __ \\        \n"
    "/     / /_  / /| |  / // /   / __/ / / / /         \n"
    "/    / __/ / ___ |_/ // /___/ /___/ /_/ /          \n"
    "/   /_/   /_/  |_/___/_____/_____/_____/           \n"
    " _________________________________________________ \n"
};
static char const                 * kSuccessBanner      = {
    " __________________________________________________\n"
    "/      _____ __  ______________________________    \n"
    "/     / ___// / / / ____/ ____/ ____/ ___/ ___/    \n"
    "/     \\__ \\/ / / / /   / /   / __/  \\__ \\__ \\ \n"
    "/    ___/ / /_/ / /___/ /___/ /___ ___/ /__/ /     \n"
    "/   /____/\\____/\\____/\\____/_____//____/____/   \n"
    " __________________________________________________\n"
};


/*******************************************************************************
 * Sets the given result and exists the test thread
*******************************************************************************/
static void TerminateTest(KeyInputTestResults result) {
    s_eResult = result;
    s_iThread->Terminate(s_iThread->GetCurrentThread());
}

/*******************************************************************************
 * Prompts the user for the next test
*******************************************************************************/
static void PromptForNextTest(void) {
    if (++s_nCurrentTest < kTestsCount) {
        const char* keyName = s_pKeyInput->GetKeyNameFromKeyValue(kTests[s_nCurrentTest].nKey);
        switch (kTests[s_nCurrentTest].eTest) {
            case kTestKeyA:
            case kTestKeyB:
            case kTestKeyC:
            case kTestKeyD:
            case kTestKeyE:
            case kTestKeyF:
                LTLOG("Key.test", "-----> Please press and release '%s' key...", keyName);
                break;
            case kTestKeyHold:
                LTLOG("key.hold", "-----> Please press and hold '%s' key for %lld seconds to test key hold", keyName, LT_Ps64(LTTime_GetSeconds(kKeyHoldTime)));
                break;
            case kTestKeyUnstuck:
                s_pKeyInput->HandleKeyStuck();
                LTLOG("key.unstuck", "-----> Please release the key to test the key unstuck function");
                break;
            default:
                LTLOG_REDALERT("test.unknown", "Unknown test");
                TerminateTest(kKeyInputUnknowTest);
                break;
        }
    } else {
        LTLOG("tests.done", "Tests completed");
        TerminateTest(kKeyInputSuccess);
    }
}

/*******************************************************************************
 * Key press event handler. Checks that the correct key was pressed
*******************************************************************************/
static void InputKeyPressEventProc(LTDeviceKeyInputValue key, void * pClientData) {
    if (key == kTests[s_nCurrentTest].nKey) {
        if (lt_strcmp(s_pClientDataTest, (const char*)pClientData) != 0) {
            LTLOG_REDALERT("key.press.clientdata.invalid", "Invalid client data received");
            TerminateTest(kKeyInputInvalidClientData);
        } else {
            if (!s_pKeyInput->IsAnyKeyDown())  {
                LTLOG_REDALERT("key.press.anykeydown", "No keys are pressed when one is expected to be released");
                TerminateTest(kKeyInputAnyKeyDownFailed);
            }
        }
    }
}

/*******************************************************************************
 * Key release event handler. Checks that the correct button was released and
 * prompts for the next one in the sequence
*******************************************************************************/
static void InputKeyReleaseEventProc(LTDeviceKeyInputValue key, void * pClientData) {
    if (key == kTests[s_nCurrentTest].nKey) {
        if (lt_strcmp(s_pClientDataTest, (const char*)pClientData) != 0) {
            LTLOG_REDALERT("key.release.clientdata.invalid", "Invalid client data received");
            TerminateTest(kKeyInputInvalidClientData);
        } else {
            if (s_pKeyInput->IsAnyKeyDown())  {
                LTLOG_REDALERT("key.press.anykeydown", "A key is pressed when none are expected to be");
                TerminateTest(kKeyInputAnyKeyDownFailed);
            } else if (kTests[s_nCurrentTest].eTest != kTestKeyHold) {
                LTLOG("key.release.clientdata", "Key released with client data '%s'", (const char*)pClientData);
                PromptForNextTest();
            }
        }
    } else {
        const char* keyName = s_pKeyInput->GetKeyNameFromKeyValue(key);
        const char* expectedKeyName = s_pKeyInput->GetKeyNameFromKeyValue(kTests[s_nCurrentTest].nKey);
        LTLOG("key.release.wrong", "Wrong key!\n  key: '%s'\n  expected '%s'", keyName, expectedKeyName);
    }
}

/*******************************************************************************
 * Key hold event handler
*******************************************************************************/
static void InputKeyHoldEventProc(LTDeviceKeyInputValue key, void * pClientData) {
    if (kTests[s_nCurrentTest].eTest == kTestKeyHold) {
        if (lt_strcmp(s_pClientDataTest, (const char*)pClientData) != 0) {
            LTLOG_REDALERT("key.hold.clientdata.invalid", "Invalid client data received");
            TerminateTest(kKeyInputInvalidClientData);
        } else {
            if (key == kTests[s_nCurrentTest].nKey) {
                LTLOG("key.hold.clientdata", "Received key hold event for key with client data '%s'", (const char*)pClientData);
                PromptForNextTest();
            } else {
                const char* keyName = s_pKeyInput->GetKeyNameFromKeyValue(key);
                const char* expectedKeyName = s_pKeyInput->GetKeyNameFromKeyValue(kTests[s_nCurrentTest].nKey);
                LTLOG_REDALERT("key.hold.wrongkey", "Received key hold event for the wrong key. got: '%s'\n  expected '%s'...please try again", keyName, expectedKeyName);
            }
        }
    }
}
/*******************************************************************************
 * Key unstuck event handler. Checks that the unstuck button check worked after
 * the HandleKeyStuck() was called
*******************************************************************************/
static void InputKeyUnstuckEventProc(LTDeviceKeyInputValue key, void * pClientData) {
    LT_UNUSED(key);
    if (lt_strcmp(s_pClientDataTest, (const char*)pClientData) != 0) {
        LTLOG_REDALERT("key.unstuck.clientdata.invalid", "Invalid client data received");
        TerminateTest(kKeyInputInvalidClientData);
    } else {
        LTLOG("key.unstuck.clientdata", "key unstuck with client data '%s'", (const char*)pClientData);
        PromptForNextTest();
    }
}

/*******************************************************************************
 * Thread start for the key input test
*******************************************************************************/
static bool OnStartKeyInputTests(void) {
    if (s_pKeyInput != NULL) {
        // register for all notifications
        LTLOG("thread.start", "Test thread started. Registering for events");
        s_pKeyInput->RegisterForKeyPress(InputKeyPressEventProc, NULL, (void *)s_pClientDataTest);
        s_pKeyInput->RegisterForKeyRelease(InputKeyReleaseEventProc, NULL,  (void *)s_pClientDataTest);
        s_pKeyInput->RegisterForKeyHold(InputKeyHoldEventProc, kKeyHoldTestKey, kKeyHoldTime, NULL,  (void *)s_pClientDataTest);
        s_pKeyInput->RegisterForKeyUnstuck(InputKeyUnstuckEventProc, NULL,  (void *)s_pClientDataTest);
        // enable the device
        s_pKeyInput->Enable(true);

        s_nCurrentTest = -1;
        PromptForNextTest();
        return true;
    }
    s_eResult = kKeyInputDeviceLibraryLoadFailed;
    return false;
}

/*******************************************************************************
 * Thread end for the key input test
*******************************************************************************/
static void OnEndKeyInputTests(void) {
    if (s_pKeyInput != NULL) {
        // unregister from all notifications
        LTLOG("thread.end", "Test thread ended. Unregistering from events");
        // disable the device
        s_pKeyInput->Enable(false);
        s_pKeyInput->UnregisterFromKeyPress(InputKeyPressEventProc);
        s_pKeyInput->UnregisterFromKeyRelease(InputKeyReleaseEventProc);
        s_pKeyInput->UnregisterFromKeyHold(InputKeyHoldEventProc, kKeyHoldTestKey);
        s_pKeyInput->UnregisterFromKeyUnstuck(InputKeyUnstuckEventProc);
    }
}

/*******************************************************************************
 * Start the key-test thread
*******************************************************************************/
static int RunKeyInputTest(void) {
    s_pKeyInput = (LTDeviceKeyInput*)LT_GetCore()->OpenLibrary("LTDeviceKeyInput");
    if (s_pKeyInput == NULL) {
        LTLOG_REDALERT("library.open.fail", "Unable to open LTDeviceKeyInput Device library");
        s_eResult = kKeyInputDeviceLibraryLoadFailed;
    } else {
        if (s_iThread != NULL) {
            LTThread hThread = LT_GetCore()->CreateThread("KeyInputTest");
            if (hThread == 0) {
                LTLOG_REDALERT("thread.create.fail", "Unable to create LTThread for keyinput test");
                s_eResult = kKeyInputTestTheadStartFailed;
            } else {
                // start the thread that registers for keyinput events
                s_iThread->SetStackSize(hThread, kStackSize);
                s_iThread->Start(hThread, OnStartKeyInputTests, OnEndKeyInputTests);
                // wait until the test is completed
                s_iThread->WaitUntilFinished(hThread, LTTime_Infinite());
                s_iThread->Destroy(hThread);
            }
            LT_GetCore()->CloseLibrary((LTLibrary*)s_pKeyInput);
            s_pKeyInput = NULL;
        }
    }
    if (s_eResult == kKeyInputSuccess) {
        LT_GetCore()->ConsolePutString(kSuccessBanner);
    } else {
        LT_GetCore()->ConsolePutString(kFailedBanner);
    }
    return s_eResult;
}

/*******************************************************************************
 * Initialize the unit test
*******************************************************************************/
static bool UnitTestLTDeviceKeyInputImpl_LibInit(void) {
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return (s_iThread != NULL);
}

/*******************************************************************************
 * Finalize the unit test
*******************************************************************************/
static void UnitTestLTDeviceKeyInputImpl_LibFini(void) {
    s_iThread = NULL;
}

/*******************************************************************************
 * Unit test run function
*******************************************************************************/
static int UnitTestLTDeviceKeyInputImpl_Run(int argc, char const ** argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    return RunKeyInputTest();
}

/*******************************************************************************
 * Interface defs
*******************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceKeyInput, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceKeyInput, UnitTestLTDeviceKeyInputImpl_Run, 1024) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  08-Feb-21   vitellius   created
 */
