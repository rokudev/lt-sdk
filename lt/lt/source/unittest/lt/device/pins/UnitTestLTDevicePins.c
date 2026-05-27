/*******************************************************************************
 * LTDevicePins Unit Test
 *
 * Test LTDevicePins and <platform>DriverPins.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/device/pins/LTDevicePins.h>

#include <tilt/JiltEngine.h>

static JiltEngine   * s_engine;
static LTDevicePins * s_pPins;
static ILTThread    * s_pIThread;

/* Test bank of input pins */
static void InputPinBankTest(Tilt * tilt, LTDeviceUnit hPinBank) {
    ILTDriverPins_InputBank * pII = lt_gethandleinterface(ILTDriverPins_InputBank, hPinBank);
    TILT_ASSERT_TRUE(tilt, pII != NULL, "could not get input pin bank interface");
    u32 nPins = pII->GetNumPins(hPinBank);
    TILT_ASSERT_TRUE(tilt, nPins > 0, "No pins in bank");
    TILT_ASSERT_TRUE(tilt, nPins <= 32, "Can't be more than 32 pins in bank");
    u32 nValue = pII->Read(hPinBank);
    TILT_DEBUG(tilt, "This input pin bank has %lu pin%s and reads as follows: 0x%x",
                  LT_Pu32(nPins), nPins == 1 ? "" : "s", nValue);
}

/* Test bank of output pins */
static void OutputPinBankTest(Tilt * tilt, LTDeviceUnit hPinBank) {
    ILTDriverPins_OutputBank * pIO = lt_gethandleinterface(ILTDriverPins_OutputBank, hPinBank);
    TILT_ASSERT_TRUE(tilt, pIO != NULL, "could not get input pin bank interface");
    u32 nPins = pIO->GetNumPins(hPinBank);
    TILT_ASSERT_TRUE(tilt, nPins > 0, "No pins in bank");
    TILT_ASSERT_TRUE(tilt, nPins <= 32, "Can't be more than 32 pins in bank");
    TILT_DEBUG(tilt, "This output pin bank has %lu pin%s", LT_Pu32(nPins), nPins == 1 ? "" : "s");
    pIO->ConfigureOutputType(hPinBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
    for (u32 bits = 0x1; nPins; --nPins, bits <<= 1) {
        pIO->Set(hPinBank, 0);
        u32 readBits = pIO->Read(hPinBank);
        TILT_EXPECT_TRUE(tilt, readBits == 0, "pin value should be 0");
        if (readBits != 0) TILT_DEBUG(tilt, "pin value read %lu", LT_Pu32(readBits));
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIO->Set(hPinBank, bits);
        readBits = pIO->Read(hPinBank);
        TILT_EXPECT_TRUE(tilt, readBits == bits, "read bits mismatch");
        if (readBits != bits) TILT_DEBUG(tilt, "pin value %lu should be %lu", LT_Pu32(readBits), LT_Pu32(bits));
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIO->Set(hPinBank, 0);
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIO->Set(hPinBank, bits);
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIO->Set(hPinBank, 0);
        s_pIThread->Sleep(LTTime_Milliseconds(50));
    }
};

/* Test bank of bidirectional pins */
static void BidirectionalPinBankTest(Tilt * tilt, LTDeviceUnit hPinBank) {
    ILTDriverPins_BidirectionalBank * pIB = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPinBank);
    TILT_ASSERT_TRUE(tilt, pIB != NULL, "could not get input pin bank interface");
    u32 nPins = pIB->GetNumPins(hPinBank);
    TILT_EXPECT_TRUE(tilt, nPins > 0, "No pins in bank");
    TILT_ASSERT_TRUE(tilt, nPins <= 32, "Can't be more than 32 pins in bank");
    pIB->ConfigureAsInput(hPinBank, kLTDevicePin_PinConfiguration_PullType_PullUp);
    u32 readBits = pIB->Read(hPinBank);
    s_pIThread->Sleep(LTTime_Milliseconds(50));
    TILT_DEBUG(tilt, "This bidirectional pin bank has %lu pin%s and reads as follows: 0x%x",
                  LT_Pu32(nPins), nPins == 1 ? "" : "s", readBits);
    pIB->ConfigureAsOutput(hPinBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
    for (u32 bits = 0x1; nPins; --nPins, bits <<= 1) {
        /* Test as output */
        pIB->Set(hPinBank, 0);
        u32 readBits = pIB->Read(hPinBank);
        TILT_EXPECT_TRUE(tilt, readBits == 0, "pin value should be 0");
        if (readBits != 0) TILT_DEBUG(tilt, "pin value read %lu", LT_Pu32(readBits));
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIB->Set(hPinBank, bits);
        readBits = pIB->Read(hPinBank);
        TILT_EXPECT_TRUE(tilt, readBits == bits, "read bits (%u != %u) mismatch", readBits, bits);
        if (readBits != bits) TILT_DEBUG(tilt, "pin value %lu should be %lu", LT_Pu32(readBits), LT_Pu32(bits));
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIB->Set(hPinBank, 0);
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIB->Set(hPinBank, bits);
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        pIB->Set(hPinBank, 0);
        s_pIThread->Sleep(LTTime_Milliseconds(50));
        /* Exercise input */
        pIB->ConfigureAsInput(hPinBank, kLTDevicePin_PinConfiguration_PullType_PullUp);
        s_pIThread->Sleep(LTTime_Milliseconds(10));
        readBits = pIB->Read(hPinBank);
        bool bState = false;
        /* Exercise toggle */
        for (u32 i = 0; i < 10; i++) {
            pIB->ConfigureAsOutput(hPinBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
            pIB->Set(hPinBank, bState ? 0 : 1);
            bState = !bState;
        }
        /* Exercise direction change */
        pIB->Set(hPinBank, 0);
        s_pIThread->Sleep(LTTime_Milliseconds(50));
        pIB->Set(hPinBank, 1);
        s_pIThread->Sleep(LTTime_Milliseconds(5));
        pIB->Set(hPinBank, 0);
        /* Input -> Output push/pull */
        pIB->ConfigureAsInput(hPinBank, kLTDevicePin_PinConfiguration_PullType_PullUp);
        readBits= pIB->Read(hPinBank);
        pIB->ConfigureAsOutput(hPinBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        pIB->Set(hPinBank, 0);
        s_pIThread->Sleep(LTTime_Milliseconds(15));
        pIB->Set(hPinBank, 1);
        s_pIThread->Sleep(LTTime_Milliseconds(100));
        pIB->Set(hPinBank, 0);
    }
}

/* Test all pins on all LTDevicePins device units */
static void TestAllPinBanks(Tilt * tilt) {
    u32 nDeviceUnits = s_pPins->GetNumDeviceUnits();
    TILT_ASSERT_TRUE(tilt, nDeviceUnits > 0, "No device units present?");
    TILT_DEBUG(tilt, "LTDevicePins reports %lu Device Unit%s", LT_Pu32(nDeviceUnits), nDeviceUnits == 1 ? "" : "s");
    for (u32 nDevice = 0; nDevice < nDeviceUnits; ++nDevice) {
        char const * pBankName = NULL;
        TILT_DEBUG(tilt, "Pin bank number %u", nDevice);
        TILT_ASSERT_TRUE(tilt, s_pPins->GetBankNameFromUnitNumber(nDevice, &pBankName),
                            "Unable to obtain bank name for device unit");
        TILT_ASSERT_TRUE(tilt, pBankName != NULL, "Provided bank name is NULL");
        TILT_DEBUG(tilt, "Pin bank name %s", pBankName);
        u32 nBankNumber = LT_U32_MAX;
        TILT_ASSERT_TRUE(tilt, s_pPins->GetUnitNumberFromBankName(pBankName, &nBankNumber),
                            "Unable to obtain bank number from name");
        TILT_ASSERT_TRUE(tilt, nBankNumber == nDevice, "Inconsistent pin bank name and number");
        LTDevicePin_PinType pinType = kLTDevicePin_PinType_Invalid;
        TILT_ASSERT_TRUE(tilt, s_pPins->GetBankTypeFromUnitNumber(nDevice, &pinType),
                            "Unable to obtain bank type for pin bank");
        if (pinType == kLTDevicePin_PinType_Invalid) {
            TILT_DEBUG(tilt, "Skipping invalid bank %u", nDevice);
            continue;
        }
        LTDeviceUnit hPinBank = s_pPins->CreateDeviceUnitHandle(nDevice);
        TILT_ASSERT_TRUE(tilt, hPinBank, "Unable to obtain device unit handle for %u", nDevice);
        switch (pinType) {
            case kLTDevicePin_PinType_Input:
                TILT_DEBUG(tilt, "Testing input bank %s", pBankName);
                InputPinBankTest(tilt, hPinBank);
                break;
            case kLTDevicePin_PinType_Output:
                TILT_DEBUG(tilt, "Testing output bank %s", pBankName);
                OutputPinBankTest(tilt, hPinBank);
                break;
            case kLTDevicePin_PinType_Bidirectional:
                TILT_DEBUG(tilt, "Testing bidirectional bank %s", pBankName);
                BidirectionalPinBankTest(tilt, hPinBank);
                break;
        }
        LT_GetCore()->DestroyHandle(hPinBank);
    }
}

static void BeforeAllTests(Tilt * tilt) {
    s_pIThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_pPins = lt_openlibrary(LTDevicePins);
    TILT_EXPECT_TRUE(tilt, s_pPins != NULL, "Cannot open LTDevicePins");
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_pPins);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestAllPinBanks, "All Pin Banks", "Test all pins on all device units",  0 },
};

static int UnitTestLTDevicePinsImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDevicePinsImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDevicePinsImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePins, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePins, UnitTestLTDevicePinsImpl_Run, 1536) LTLIBRARY_DEFINITION;
