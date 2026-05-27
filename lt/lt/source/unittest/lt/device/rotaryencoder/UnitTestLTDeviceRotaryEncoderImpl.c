/*******************************************************************************
 * Rotary encoder Unit Test
 *
 * Test LTDeviceRotaryEncoder and <platform>DriverRotaryEncoder.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/rotaryencoder/LTDeviceRotaryEncoder.h>

DEFINE_LTLOG_SECTION("unitest.rotaryencoder");

/*******************************************************************************
 * Test results returned by the unit tests
*******************************************************************************/
typedef enum {
    kRotaryEncoderSuccess                               = 0,
    kRotaryEncoderClientData                            = 1,
    kRotaryEncoderInvalidEncoderName                    = 2,
    // load failures
    kRotaryEncoderDeviceLibraryLoadFailed               = 20,
    kRotaryEncoderTestTheadStartFailed                  = 21
} UnitTestRotaryEncoderResults;

/*******************************************************************************
 * Consts
*******************************************************************************/
static const s32                kTestPosition           = 100;
static const char             * kClientDataTest         = "Client data test";

/*******************************************************************************
 * Interfaces and libraries
*******************************************************************************/
static ILTThread              * s_iThread               = NULL;
static LTDeviceRotaryEncoder  * s_pRotaryEncoder        = NULL;

static LTDeviceUnit             s_hDevice               = 0;

static u32                      s_nNumDevices           = 0;
static u32                      s_nCurrTestDeviceNum    = 0;

/*******************************************************************************
 * Test data and result
*******************************************************************************/
static UnitTestRotaryEncoderResults s_eResult           = kRotaryEncoderSuccess;
// desired position value
static s32 s_nRequiredPosition                          = 0;

/*******************************************************************************
 * Banners
*******************************************************************************/
static const char * kFailedBanner                       = {
    " _________________________________________________ \n"
    "/       _________    ______    __________          \n"
    "/      / ____/   |  /  _/ /   / ____/ __ \\        \n"
    "/     / /_  / /| |  / // /   / __/ / / / /         \n"
    "/    / __/ / ___ |_/ // /___/ /___/ /_/ /          \n"
    "/   /_/   /_/  |_/___/_____/_____/_____/           \n"
    " _________________________________________________ \n"
};
static const char * kSuccessBanner                      = {
    " __________________________________________________\n"
    "/      _____ __  ______________________________    \n"
    "/     / ___// / / / ____/ ____/ ____/ ___/ ___/    \n"
    "/     \\__ \\/ / / / /   / /   / __/  \\__ \\__ \\ \n"
    "/    ___/ / /_/ / /___/ /___/ /___ ___/ /__/ /     \n"
    "/   /____/\\____/\\____/\\____/_____//____/____/   \n"
    " __________________________________________________\n"
};
static const char * kRotationBannerCW                   = {
    "   -->\n"
    "  /   \\    (Position: %d, speed: %d/sec)\n"
    "  \\   /\n"
    "   <--\n"
};
static const char * kRotationBannerCCW                  = {
    "   <--\n"
    "  /   \\    (Position: %d, speed: %d/sec)\n"
    "  \\   /\n"
    "   -->\n"
};

// prototypes
static void StartTest(u32 nDeviceNumber);

/*******************************************************************************
 * Sets the given result and exists the test thread
*******************************************************************************/
static void TerminateTest(UnitTestRotaryEncoderResults result) {
    s_eResult = result;

    s_pRotaryEncoder->UnregisterFromPositionChanges(s_hDevice);

    s_iThread->Terminate(s_iThread->GetCurrentThread());
}

/*******************************************************************************
 * rotary encoder event handler
*******************************************************************************/
static void RotaryEncoderPositionChangedEventProc(RotaryEncoderPosition position, void * pClientData) {
    LT_UNUSED(pClientData);

    if (lt_strcmp((const char *)pClientData, kClientDataTest) != 0) {
        LTLOG_REDALERT("clientdata.test", "Client data does not match '%s'", kClientDataTest);
        TerminateTest(kRotaryEncoderClientData);
    }

    if (s_nRequiredPosition > 0) {
        // print the rotation banner with the new position
        if (position.direction == kRotationDirectionCW) {
            LT_GetCore()->ConsolePrint(kRotationBannerCW, position.nPosition, position.nRotationSpeed);
        }
        if (position.nPosition >= s_nRequiredPosition) {
            // now test negative position
            s_nRequiredPosition = -kTestPosition;
            LTLOG("test.rotate", "-----> Please rotate encoder #%d counter-clockwise to %d", (int)s_nCurrTestDeviceNum, (int)s_nRequiredPosition);
        }
    } else {
        // print the new position
        if (position.direction == kRotationDirectionCCW) {
            LT_GetCore()->ConsolePrint(kRotationBannerCCW, position.nPosition, position.nRotationSpeed);
        }
        if (position.nPosition <= s_nRequiredPosition) {
            LTLOG("position.reached", "Test completed for device unit %d", (int)s_nCurrTestDeviceNum);
            // start the test for the next device
            if (++s_nCurrTestDeviceNum < s_nNumDevices) {
                StartTest(s_nCurrTestDeviceNum);
            } else {
                TerminateTest(kRotaryEncoderSuccess);
            }
        }
    }
}

/*******************************************************************************
 * Starts the tests for the given device number
*******************************************************************************/
static void StartTest(u32 nDeviceNumber) {

    if (s_hDevice != 0) {
        s_pRotaryEncoder->UnregisterFromPositionChanges(s_hDevice);
        s_pRotaryEncoder->Destroy(s_hDevice);
        s_hDevice = 0;
    }

    // test the encoder name
    const char * pEncoderName = NULL;
    if (!s_pRotaryEncoder->GetEncoderNameFromUnitNumber(nDeviceNumber, &pEncoderName)) {
        LTLOG_REDALERT("encoder.name.fail", "Failed to get the encoder name for device %d", (int)nDeviceNumber);
        TerminateTest(kRotaryEncoderInvalidEncoderName);
    } else {
        LTLOG("encoder.name", "The encoder name is '%s'", pEncoderName);
        // register for all notifications
        LTLOG("test.start", "Starting test for device #%d. Registering for events...", (int)nDeviceNumber);
        s_nRequiredPosition = kTestPosition;

        // actual client that will be used
        s_hDevice = s_pRotaryEncoder->CreateDeviceUnitHandle(nDeviceNumber);
        if (s_eResult == kRotaryEncoderSuccess) {
            LTLOG_DEBUG("client.register", "Registering client (0x%lx)", LT_Pu32(s_hDevice));
            s_pRotaryEncoder->RegisterForPositionChanges(s_hDevice, RotaryEncoderPositionChangedEventProc, LTTime_Zero(), (void *)kClientDataTest);
        }

        ILTDriverRotaryEncoder * piIntercoderDriver = lt_gethandleinterface(ILTDriverRotaryEncoder, s_hDevice);
        if (s_eResult == kRotaryEncoderSuccess && piIntercoderDriver != NULL) {
            piIntercoderDriver->EnableAcceleration(s_hDevice, true);
            LTLOG("test.rotate", "-----> Please rotate encoder #%d clockwise to %d", (int)nDeviceNumber, (int)s_nRequiredPosition);
        }
    }
}

/*******************************************************************************
 * Thread start for the rotary ecoder test
*******************************************************************************/
static bool OnStartRotaryEncoderTests(void) {
    if (s_pRotaryEncoder != NULL) {
        s_nNumDevices = s_pRotaryEncoder->GetNumDeviceUnits();
        if (s_nNumDevices > 0) {
            s_nCurrTestDeviceNum = 0;
            StartTest(s_nCurrTestDeviceNum);
        }
        return true;
    }
    // shouldn't happen
    s_eResult = kRotaryEncoderDeviceLibraryLoadFailed;
    return false;
}

/*******************************************************************************
 * Thread end for the rotary encoder test
*******************************************************************************/
static void OnEndRotaryEncoderTests(void) {
    if (s_pRotaryEncoder != NULL) {
        // unregister from all notifications
        if (s_hDevice != 0) {
            s_pRotaryEncoder->Destroy(s_hDevice);
            s_hDevice = 0;
        }
    }
}

/*******************************************************************************
 * Start the rotary encoder thread
*******************************************************************************/
static int RunRotaryEncoderTest(void) {
    s_pRotaryEncoder = (LTDeviceRotaryEncoder *)LT_GetCore()->OpenLibrary("LTDeviceRotaryEncoder");
    if (s_pRotaryEncoder == NULL) {
        LTLOG_REDALERT("fail.open", "Unable to open LTDeviceRotaryEncoder Device library");
        s_eResult = kRotaryEncoderDeviceLibraryLoadFailed;
    } else {
        if (s_iThread != NULL) {
            LTThread hThread = LT_GetCore()->CreateThread("UnitTestRotaryEncoder");
            if (hThread == 0) {
                LTLOG_REDALERT("thread.start.failed", "Unable to create LTThread for RotaryEncoder test");
                s_eResult = kRotaryEncoderTestTheadStartFailed;
            } else {
                // start the thread that registers for RotaryEncoder events
                s_iThread->SetStackSize(hThread, 1024);
                s_iThread->Start(hThread, OnStartRotaryEncoderTests, OnEndRotaryEncoderTests);
                // wait until the test is completed
                s_iThread->WaitUntilFinished(hThread, LTTime_Infinite());
                s_iThread->Destroy(hThread);
            }
            LT_GetCore()->CloseLibrary((LTLibrary*)s_pRotaryEncoder);
            s_pRotaryEncoder = NULL;
        }
    }
    if (s_eResult == kRotaryEncoderSuccess) {
        LT_GetCore()->ConsolePutString(kSuccessBanner);
    } else {
        LT_GetCore()->ConsolePutString(kFailedBanner);
    }
    return s_eResult;
}

/*******************************************************************************
 * Initialize the unit test
*******************************************************************************/
static bool UnitTestLTDeviceRotaryEncoderImpl_LibInit(void) {
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return (s_iThread != NULL);
}

/*******************************************************************************
 * Finalize the unit test
*******************************************************************************/
static void UnitTestLTDeviceRotaryEncoderImpl_LibFini(void) {
    s_iThread = NULL;
}

/*******************************************************************************
 * Unit test run function
*******************************************************************************/
static int UnitTestLTDeviceRotaryEncoderImpl_Run(int argc, char const ** argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    return RunRotaryEncoderTest();
}

/*******************************************************************************
 * Interface defs
*******************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceRotaryEncoder, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceRotaryEncoder, UnitTestLTDeviceRotaryEncoderImpl_Run, 1024) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  08-Feb-21   vitellius   created
 */
