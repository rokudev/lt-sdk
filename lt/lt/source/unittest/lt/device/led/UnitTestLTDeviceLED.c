/*******************************************************************************
 * LED Device Test
 *
 * Test LTDeviceLED and <platform>DriverLED.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/led/LTDeviceLED.h>

#include <tilt/JiltEngine.h>

#define MAKE_COLOR(i, r, g, b)                    (i << 24 | r << 16 | g << 8 | b)

typedef struct {
    char * name;
    u8 r;
    u8 g;
    u8 b;
} LEDColorDef;
 static const LEDColorDef s_colorwheel[] = {
    { "Black",    0x00, 0x00, 0x00 },
    { "Red",      0xFF, 0x00, 0x00 },
    { "Lime",     0x00, 0xFF, 0x00 },
    { "Blue",     0x00, 0x00, 0xFF },
    { "Yellow",   0xFF, 0xFF, 0x00 },
    { "Cyan",     0x00, 0xFF, 0xFF },
    { "Magenta",  0xFF, 0x00, 0xFF },
    { "Silver",   0xC0, 0xC0, 0xC0 },
    { "Gray",     0x80, 0x80, 0x80 },
    { "Maroon",   0x80, 0x00, 0x00 },
    { "Olive",    0x80, 0x80, 0x00 },
    { "Green",    0x00, 0xFF, 0x00 },
    { "Purple",   0x80, 0x00, 0x80 },
    { "Teal",     0x00, 0x80, 0x80 },
    { "Navy",     0x00, 0x00, 0x80 },
    { "White",    0xFF, 0xFF, 0xFF },
};

enum {
    kMessageStrSize     = 128,
};

static JiltEngine                               * s_engine;
static LTDeviceLED                              * s_led;
static ILTThread                                * s_iThread          = NULL;
static ILTDriverLED_GroupType_IndicatorLamp     * s_iLEDDeviceUnit   = NULL;
static ILTDriverLED_GroupType_SevenSegmentDigit * s_iDigitDeviceUnit = NULL;
static bool                                       s_bFull            = false;

/***********************************************************************************************************
 * Test IndicatorLamp LEDs:                                                                               */
static void TestLED(Tilt * tilt, LTDeviceUnit hLED) {
    enum { kColorOff = 0,
           kColorOn = 0xFFFFFFFF };
    for (u32 nLED = 0; nLED < s_iLEDDeviceUnit->GetNumberOfElements(hLED); ++nLED) {
        TILT_DEBUG(tilt, "turn LED off");
        s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, kColorOff);
        u32 nColor = s_iLEDDeviceUnit->GetLEDColor(hLED, nLED);
        TILT_EXPECT_FALSE(tilt, nColor != kColorOff, "GetLEDColor() for index %lu returns %08lX, s/b %08lX",
                                                     LT_Pu32(nLED), LT_Pu32(nColor), LT_Pu32(kColorOff));
        if (nColor != kColorOff) return;
        s_iThread->Sleep(LTTime_Milliseconds(s_bFull ? 500 : 200));
        TILT_DEBUG(tilt, "turn LED on");
        s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, kColorOn);
        nColor = s_iLEDDeviceUnit->GetLEDColor(hLED, nLED);
        TILT_EXPECT_FALSE(tilt, nColor != kColorOn, "GetLEDColor() for index %lu returns %08lX, s/b %08lX",
                                                    LT_Pu32(nLED), LT_Pu32(nColor), LT_Pu32(kColorOn));
        if (nColor != kColorOn) return;
        s_iThread->Sleep(LTTime_Milliseconds(s_bFull ? 500 : 200));
        if (s_bFull) {
            TILT_DEBUG(tilt, "turn LED off");
            s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, kColorOff);
            s_iThread->Sleep(LTTime_Milliseconds(s_bFull ? 500 : 200));
            TILT_DEBUG(tilt, "fade in");
            for (u32 i = 0; i <= 255; i += 5) {
                s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, MAKE_COLOR(i, i, i, i));
                s_iThread->Sleep(LTTime_Milliseconds(20));
            }
            TILT_DEBUG(tilt, "fade out");
            for (u32 i = 255; i > 0; i -= 5) {
                s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, MAKE_COLOR(i, i, i, i));
                s_iThread->Sleep(LTTime_Milliseconds(20));
            }
            for (u32 i = 0; i < sizeof(s_colorwheel) / sizeof(s_colorwheel[0]) - 1; ++i) {
                TILT_DEBUG(tilt, "fading from %s to %s", s_colorwheel[i].name, s_colorwheel[i+1].name);
                u8 r = s_colorwheel[i].r;
                u8 g = s_colorwheel[i].g;
                u8 b = s_colorwheel[i].b;
                while (   (r != s_colorwheel[i + 1].r)
                       || (g != s_colorwheel[i + 1].g)
                       || (b != s_colorwheel[i + 1].b)) {
                    if      (r < s_colorwheel[i + 1].r) r += 1;
                    else if (r > s_colorwheel[i + 1].r) r -= 1;
                    if      (g < s_colorwheel[i + 1].g) g += 1;
                    else if (g > s_colorwheel[i + 1].g) g -= 1;
                    if      (b < s_colorwheel[i + 1].b) b += 1;
                    else if (b > s_colorwheel[i + 1].b) b -= 1;
                    s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, MAKE_COLOR(0x80, r, g, b));
                    s_iThread->Sleep(LTTime_Milliseconds(2));
                }
            }
        }
        TILT_DEBUG(tilt, "turn LED off");
        s_iLEDDeviceUnit->SetLEDColor(hLED, nLED, kColorOff);
        s_iThread->Sleep(LTTime_Milliseconds(20));
    }
}

/***********************************************************************************************************
 * Test seven-segment displays:                                                                           */

/* Exercise a single seven-segment digit: */
static void TestSevenSegmentDigit(Tilt * tilt, LTDeviceUnit hLEDGroup, u32 nDigitIndex, u32 delay_ms) {
    TILT_INFO(tilt, "exercise digit %lu", LT_Pu32(nDigitIndex));
    LTTime delay = LTTime_Milliseconds(delay_ms);
    /* Blink the digit a couple of times - all segments and decimal point: */
    for (u32 i = 2; i; --i) {
        s_iDigitDeviceUnit->SetSegmentBitPattern(hLEDGroup, nDigitIndex, 0xFF);
        s_iThread->Sleep(delay);
        s_iDigitDeviceUnit->SetSegmentBitPattern(hLEDGroup, nDigitIndex, 0);
        s_iThread->Sleep(delay);
    }
    /* Activate each segment (and decimal point) individually: */
    u32 nBit = 0;
    s8 bitPattern = 1;
    for (nBit = 8; nBit; --nBit, bitPattern <<= 1) {
        s_iDigitDeviceUnit->SetSegmentBitPattern(hLEDGroup, nDigitIndex, (u8) bitPattern);
        s_iThread->Sleep(delay);
    }
    /* Activate segments progressively until all are lit: */
    for (nBit = 8, bitPattern = (s8)0x80; nBit; --nBit, bitPattern >>= 1) {
        s_iDigitDeviceUnit->SetSegmentBitPattern(hLEDGroup, nDigitIndex, (u8) bitPattern);
        s_iThread->Sleep(delay);
    }
    /* Activate only the decimal point: */
    s_iDigitDeviceUnit->SetSegmentBitPattern(hLEDGroup, nDigitIndex, 0x80);
    s_iThread->Sleep(delay);
    /* Activate the digit with its digit index: */
    s_iDigitDeviceUnit->SetDigit(hLEDGroup, nDigitIndex, nDigitIndex, false);
    s_iThread->Sleep(delay);
}

static void TestDigitValue(Tilt * tilt, LTDeviceUnit hLEDGroup, u8 nValue, u32 nDigits, u32 delay_ms) {
    TILT_INFO(tilt, "Value test %c", (char)nValue);
    enum { kBufSize = 40 }; /* deliberately oversized.  The driver should use only what it needs. */
    u8 buf[kBufSize];
    /* Fill the entire buffer with the value.  Every other character is used for the decimal point,
     * and will be ignored if not '.' (leaving the decimal point off): */
    lt_memset(buf, nValue, kBufSize);
    s_iDigitDeviceUnit->SetDigits(hLEDGroup, 0, nDigits, (char *)buf);
    s_iThread->Sleep(LTTime_Milliseconds(delay_ms));
    u8 * pChar = buf + 1;
    /* Fill in the decimal points: */
    for (u32 c = 0; c < kBufSize; c += 2, pChar += 2) *pChar = '.';
    s_iDigitDeviceUnit->SetDigits(hLEDGroup, 0, nDigits, (char *)buf);
    s_iThread->Sleep(LTTime_Milliseconds(delay_ms));
}

static void TestSevenSegmentDisplay(Tilt * tilt, LTDeviceUnit hLEDGroup) {
    u32 nDigits = s_iDigitDeviceUnit->GetNumberOfDigits(hLEDGroup);
    /* Take the display out of low-power mode (which is its initialized state): */
    s_iDigitDeviceUnit->SetLowPowerMode(hLEDGroup, false);
    /* Toggle display test mode (all segments on, full intensity) on and off: */
    TILT_INFO(tilt, "Display test Mode");
    s_iDigitDeviceUnit->SetDisplayTestMode(hLEDGroup, true);
    s_iThread->Sleep(LTTime_Seconds(s_bFull ? 2 : 1));
    s_iDigitDeviceUnit->SetDisplayTestMode(hLEDGroup, false);
    s_iDigitDeviceUnit->SetColor(hLEDGroup, 0x80000000);
    s_iDigitDeviceUnit->Clear(hLEDGroup);
    s_iThread->Sleep(LTTime_Milliseconds(s_bFull ? 500 : 300));
    if (s_bFull) {
        for (u8 nValue = '0'; nValue <= '9'; ++nValue)
            TestDigitValue(tilt, hLEDGroup, nValue, nDigits, 500);
        for (u8 nValue = 'A'; nValue <= 'F'; ++nValue) {
            /* Test both upper- and lower-case hexadecimal values: */
            TestDigitValue(tilt, hLEDGroup, nValue,        nDigits, 250);
            TestDigitValue(tilt, hLEDGroup, nValue | 0x20, nDigits, 250);
        }
        TILT_DEBUG(tilt, "Clear test");
        s_iDigitDeviceUnit->Clear(hLEDGroup);
        s_iThread->Sleep(LTTime_Seconds(1));
    }
    /* Exercise each digit individually.  This leaves the display with each
     * digit showing its digit index, with no decimal points lit.
     * For the non-brief test, this loop goes two past the number of
     * available digits, to test protection against out-of-bounds accesses: */
    u32 nOutOfBounds = s_bFull ? 2 : 0;
    for (u32 nDigit = 0; nDigit < nDigits + nOutOfBounds; ++nDigit)
        TestSevenSegmentDigit(tilt, hLEDGroup, nDigit, 100);
    enum { kBufSize = 40 }; /* deliberately oversized.  The driver should use only what it needs. */
    u8 buf[kBufSize];
    lt_memset(buf, '0', kBufSize);
    if (s_bFull) {
        /* Add decimal points to the digits, wiping left to right: */
        TILT_DEBUG(tilt, "Decimal point test");
        for (u32 nDigit = 0; nDigit < nDigits + 2; ++nDigit) {
            s_iDigitDeviceUnit->SetDigit(hLEDGroup, nDigit, nDigit, true);
            s_iThread->Sleep(LTTime_Milliseconds(100));
        }
        s_iDigitDeviceUnit->SetDigits(hLEDGroup, 0, nDigits, (char *)buf);
        s_iThread->Sleep(LTTime_Milliseconds(100));
        for (u32 nDigit = 0; nDigit < nDigits; ++nDigit) {
            buf[nDigit * 2 + 1] = '.';
            s_iDigitDeviceUnit->SetDigits(hLEDGroup, 0, nDigits, (char *)buf);
            s_iThread->Sleep(LTTime_Milliseconds(100));
        }
        /* Take the display from full intensity down to dimmest.  Test color retrieval: */
        TILT_DEBUG(tilt, "intensity test - ramp down");
        for (u32 nColor = 0xF8000000; nColor; nColor -= 0x08000000) {
            s_iDigitDeviceUnit->SetColor(hLEDGroup, nColor);
            s_iThread->Sleep(LTTime_Milliseconds(50));
            u32 nReturnedColor = s_iDigitDeviceUnit->GetColor(hLEDGroup);
            TILT_EXPECT_TRUE(tilt, nReturnedColor == nColor, "color mismatch for seven-segment display returns %08lX, s/b %08lX",
                                                             LT_Pu32(nReturnedColor), LT_Pu32(nColor));
            if (nReturnedColor != nColor) return;
        }
        /* Ramp the display intensity up to half-intensity: */
        TILT_DEBUG(tilt, "intensity test - ramp up");
        for (u32 nColor = 0; nColor < 0x80000000; nColor += 0x08000000) {
            s_iDigitDeviceUnit->SetColor(hLEDGroup, nColor);
            s_iThread->Sleep(LTTime_Milliseconds(20));
        }
        s_iThread->Sleep(LTTime_Milliseconds(500));
        /* Snap to full intensity, then to lowest, then to a little above half: */
        TILT_DEBUG(tilt, "full intensity");
        s_iDigitDeviceUnit->SetColor(hLEDGroup, 0xFF000000);
        s_iThread->Sleep(LTTime_Milliseconds(500));
        TILT_DEBUG(tilt, "low intensity");
        s_iDigitDeviceUnit->SetColor(hLEDGroup, 0x00000000);
        s_iThread->Sleep(LTTime_Milliseconds(500));
        TILT_DEBUG(tilt, "mid intensity");
        s_iDigitDeviceUnit->SetColor(hLEDGroup, 0x80000000);
        s_iThread->Sleep(LTTime_Milliseconds(500));
        /* Test multi-digit control - fill the display, then set smaller and
         * smaller strings toward the middle.  Start with digits: */
        lt_memset(buf, ' ', kBufSize);
        TILT_DEBUG(tilt, "substring test: digits");
        s_iDigitDeviceUnit->SetDigits(hLEDGroup, 0, nDigits, (char *)buf);
        s_iThread->Sleep(LTTime_Milliseconds(500));
        for (u32 n = 0; n <= nDigits / 2; ++n) {
            lt_memset(buf, '0' + n, kBufSize);
            s_iDigitDeviceUnit->SetDigits(hLEDGroup, n, nDigits - n * 2, (char *)buf);
            s_iThread->Sleep(LTTime_Seconds(1));
        }
        /* Repeat the multi-digit test with bit patterns: */
        u8 const patterns[] = { 0x48, 0x24, 0x12, 0x81 };
        u32 pattern = 0;
        TILT_DEBUG(tilt, "substring test: bit patterns");
        for (u32 n = 0; n <= nDigits / 2; ++n, pattern = (pattern + 1) % sizeof patterns) {
            lt_memset(buf, patterns[pattern], kBufSize);
            s_iDigitDeviceUnit->SetSegmentBitPatterns(hLEDGroup, n, nDigits - n * 2, buf);
            s_iThread->Sleep(LTTime_Seconds(1));
        }
        /* Toggle low-power mode.  Display should shut off, then come back on with the
         * same segments lit that were lit before low-power mode: */
        TILT_DEBUG(tilt, "low-power mode on");
        s_iDigitDeviceUnit->SetLowPowerMode(hLEDGroup, true);
        s_iThread->Sleep(LTTime_Seconds(1));
        TILT_DEBUG(tilt, "low-power mode off");
        s_iDigitDeviceUnit->SetLowPowerMode(hLEDGroup, false);
        s_iThread->Sleep(LTTime_Seconds(1));
    }
    /* Clear the display, and shut the display off again: */
    TILT_DEBUG(tilt, "test complete - clear display, set low-power mode");
    lt_memset(buf, ' ', kBufSize);
    s_iDigitDeviceUnit->SetDigits(hLEDGroup, 0, nDigits, (char *)buf);
    s_iDigitDeviceUnit->SetLowPowerMode(hLEDGroup, true);
}

/***********************************************************************************************************
 * Secure a Device Unit Handle for the given LED Group (by unit number) and exercise the LED:             */

static void TestLEDDeviceUnit(Tilt * tilt, u32 nGroupNumber, LEDGroupType nLEDGroupType) {
    LTDeviceUnit hLEDGroup = s_led->CreateDeviceUnitHandle(nGroupNumber);
    TILT_EXPECT_TRUE(tilt, hLEDGroup != 0, "unable to get Device Unit handle for LED Group %lu", LT_Pu32(nGroupNumber));
    if (!hLEDGroup) {
        return;
    } else switch (nLEDGroupType) {
    case kLTDeviceLED_GroupType_IndicatorLamp:
        s_iLEDDeviceUnit = lt_gethandleinterface(ILTDriverLED_GroupType_IndicatorLamp, hLEDGroup);
        TILT_EXPECT_TRUE(tilt, s_iLEDDeviceUnit != NULL, "unable to get Device Unit interface for the LED Indicator Lamp Group");
        if (s_iLEDDeviceUnit) {
            TestLED(tilt, hLEDGroup);
        }
        break;
    case kLTDeviceLED_GroupType_SevenSegmentDigit:
        s_iDigitDeviceUnit = lt_gethandleinterface(ILTDriverLED_GroupType_SevenSegmentDigit, hLEDGroup);
        TILT_EXPECT_TRUE(tilt, s_iDigitDeviceUnit != NULL, "unable to get Device Unit interface for the LED Seven-Segment Digit Group");
        if (s_iDigitDeviceUnit) {
            TestSevenSegmentDisplay(tilt, hLEDGroup);
        }
        break;
    }
    LT_GetCore()->DestroyHandle(hLEDGroup);
}

/***********************************************************************************************************
 * Test LED Group access - read the LED Group information, and verify consistency of group number and
 * group information:                                                                                     */
static bool TestLEDGroup(Tilt * tilt, u32 nGroupNumber, LEDGroupType type) {
    LTDeviceLED_GroupDescriptor descriptor;
    bool bTestRan = true;
    bool bSuccess = s_led->GetGroupDescriptorFromUnitNumber(nGroupNumber, &descriptor);
    TILT_EXPECT_TRUE(tilt, bSuccess, "unable to get LED Group information for LED Group %lu", LT_Pu32(nGroupNumber));
    if (bSuccess) {
        if (descriptor.m_groupType != type) {
            bTestRan = false;
        } else {
            TILT_INFO(tilt, "Testing group \"%s\"", descriptor.m_pGroupName);
            TILT_DEBUG(tilt, "LED Group info: Device Unit Number %lu; Group Name: \"%s\"; Group Type: %lu; %lu element%s",
                             LT_Pu32(descriptor.m_nDeviceUnitNumber),
                             descriptor.m_pGroupName,
                             LT_Pu32((u32)descriptor.m_groupType),
                             LT_Pu32(descriptor.m_nNumElements),
                             descriptor.m_nNumElements == 1 ? "" : "s");
            TILT_EXPECT_TRUE(tilt, descriptor.m_nDeviceUnitNumber == nGroupNumber, "LED Group number %lu inconsistent with Device Unit number %lu",
                                                                                   LT_Pu32(descriptor.m_nDeviceUnitNumber), LT_Pu32(nGroupNumber));
            TILT_EXPECT_TRUE(tilt, *descriptor.m_pGroupName != '\0', "LED Group number %lu name is empty", LT_Pu32(nGroupNumber));
            TILT_EXPECT_TRUE(tilt, descriptor.m_nNumElements != 0, "LED Group number %lu reports zero elements", LT_Pu32(nGroupNumber));
            if (descriptor.m_nDeviceUnitNumber == nGroupNumber && *descriptor.m_pGroupName != '\0' && descriptor.m_nNumElements != 0) {
                u32 nGroupNumberFromName = LT_U32_MAX;
                bSuccess = s_led->GetUnitNumberFromGroupName(descriptor.m_pGroupName, &nGroupNumberFromName);
                TILT_EXPECT_TRUE(tilt, bSuccess, "Unable to get LED Group number from Group Name \"%s\"", descriptor.m_pGroupName);
                TILT_EXPECT_TRUE(tilt, nGroupNumberFromName == nGroupNumber, "LED Group number %lu from name \"%s\" inconsistent with Device Unit number %lu",
                                                                             LT_Pu32(nGroupNumberFromName), descriptor.m_pGroupName,
                                                                             LT_Pu32(nGroupNumber));
                if (bSuccess && nGroupNumberFromName == nGroupNumber) {
                    TestLEDDeviceUnit(tilt, nGroupNumber, descriptor.m_groupType);
                }
            }
        }
    }
    return bTestRan;
}

/***********************************************************************************************************
 * Open the Device library.  Secure a Device Unit handle for every available instance and perform the
 * test on each:                                                                                          */
static void RunLEDTestType(Tilt * tilt, LEDGroupType type) {
    u32 nNumDeviceUnits = s_led->GetNumDeviceUnits();
    TILT_DEBUG(tilt, "LTDeviceLED reports %lu Device Unit%s",
                     LT_Pu32(nNumDeviceUnits),
                     nNumDeviceUnits == 1 ? "" : "s");
    TILT_EXPECT_TRUE(tilt, nNumDeviceUnits > 0, "LTDeviceLED provides no Device Units");
    if (nNumDeviceUnits > 0) {
        bool bTestRan = false;
        for (u32 n = 0; n < nNumDeviceUnits; ++n)
            bTestRan |= TestLEDGroup(tilt, n, type);
        if (!bTestRan)
            TILT_REPORT_CANNOT_RUN(tilt, "No device found for Group type %d", type);
    }
}

/***********************************************************************************************************
 * Test properties                                                                                        */
static void SetTestProperties(Tilt * tilt) {
    /* This logic is a little strange (a simpler form would be to check for "full" with a default of "n"),
       but the automated tests should run in brief mode, so that needs to be the default, and I don't want
       to require "full=n". */
    const char * pBriefProperty = tilt->API->GetProperty("brief", "y");
    s_bFull = false;
    if (pBriefProperty && *pBriefProperty == 'n') s_bFull = true;
}

/***********************************************************************************************************
 * Tilt tests                                                                                             */
static void RunLEDTest(Tilt * tilt) {
    SetTestProperties(tilt);
    RunLEDTestType(tilt, kLTDeviceLED_GroupType_IndicatorLamp);
}

static void RunSevenSegmentDisplayTest(Tilt * tilt) {
    SetTestProperties(tilt);
    RunLEDTestType(tilt, kLTDeviceLED_GroupType_SevenSegmentDigit);
}

static void BeforeAllTests(Tilt * tilt) {
    s_iThread           = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_iLEDDeviceUnit    = NULL;
    s_iDigitDeviceUnit  = NULL;
    s_led = lt_openlibrary(LTDeviceLED);
    TILT_EXPECT_TRUE(tilt, s_led != NULL, "Cannot open LTDeviceLED");
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_led);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { RunLEDTest,                 "LED",             "Runs the LED tests",                      0 },
    { RunSevenSegmentDisplayTest, "SevenSegmentLED", "Runs the seven segment LED display test", 0 },
};

static int UnitTestLTDeviceLEDImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceLEDImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceLEDImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceLED, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceLED, UnitTestLTDeviceLEDImpl_Run, 1536) LTLIBRARY_DEFINITION;
