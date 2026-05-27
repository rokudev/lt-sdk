/*******************************************************************************
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTThread.h>
#include <lt/device/sdcard/LTDeviceSDCard.h>

#include <tilt/JiltEngine.h>

static JiltEngine          * s_engine;
static LTHandle              s_hUnit = 0;
static ILTThread           * s_iThread;
static LTDeviceSDCard      * s_SDCard;
static ILTSDCardDeviceUnit * s_iSDCard;

static void TestManageSDCard(Tilt * tilt) {
    bool ret;
    LTDeviceSDCard_SDCardInfo info;

    s_hUnit = s_SDCard->CreateDeviceUnitHandle(0);
    TILT_EXPECT_TRUE(tilt, s_hUnit, "Expect unit handle");
    ILTSDCardDeviceUnit * iSDCard = lt_gethandleinterface(ILTSDCardDeviceUnit, s_hUnit);
    TILT_EXPECT_TRUE(tilt, iSDCard, "Expected interface exists");

    iSDCard->Unmount(s_hUnit);

    info = iSDCard->GetSDCardInfo(s_hUnit);
    TILT_ASSERT_TRUE(tilt, info.present, "sd card present");
    TILT_ASSERT_FALSE(tilt, info.mounted, "sd card not mounted");

    ret = iSDCard->Format(s_hUnit, (LTDeviceSDCard_FormatOptions){.format = LTDeviceSDCard_Format_EXFAT});
    TILT_ASSERT_TRUE(tilt, ret, "format exfat");
    ret = iSDCard->Mount(s_hUnit);
    TILT_ASSERT_TRUE(tilt, ret, "mount exfat sdcard");
    info = iSDCard->GetSDCardInfo(s_hUnit);
    TILT_ASSERT_TRUE(tilt, info.present, "sd card present");
    TILT_ASSERT_TRUE(tilt, info.mounted, "sd card mounted");
    TILT_ASSERT_TRUE(tilt, info.fstype == LTDeviceSDCard_Format_EXFAT, "filesystem is exfat");

    ret = iSDCard->Format(s_hUnit, (LTDeviceSDCard_FormatOptions){.format = LTDeviceSDCard_Format_EXFAT});
    TILT_ASSERT_FALSE(tilt, ret, "format mounted sdcard fails");
    iSDCard->Unmount(s_hUnit);
    TILT_ASSERT_TRUE(tilt, ret, "unmount");

    ret = iSDCard->Format(s_hUnit, (LTDeviceSDCard_FormatOptions){.format = LTDeviceSDCard_Format_FAT32});
    TILT_ASSERT_TRUE(tilt, ret, "format fat32");
    ret = iSDCard->Mount(s_hUnit);
    TILT_ASSERT_TRUE(tilt, ret, "mount fat32 sdcard");
    info = iSDCard->GetSDCardInfo(s_hUnit);
    TILT_ASSERT_TRUE(tilt, info.present, "card is present");
    TILT_ASSERT_TRUE(tilt, info.mounted, "card is mounted");
    TILT_ASSERT_TRUE(tilt, info.fstype == LTDeviceSDCard_Format_FAT32, "filesystem is fat32");
    iSDCard->Unmount(s_hUnit);
    TILT_ASSERT_TRUE(tilt, ret, "unmount");

    s_SDCard->Destroy(s_hUnit);
    s_hUnit = 0;
}

static void Callback(LTDeviceSDCard_SDCardInfo *info, void * pClientData) {
    Tilt * tilt = pClientData;
    TILT_INFO(tilt, "SD card is %s", info->present ? "present" : "not present");
    s_engine->API->SignalTestCompletion(s_engine, "TestSDCardHotplug");
}

static void RegisterCallback(void *pClientData) {
    Tilt * tilt = pClientData;
    s_iSDCard->OnSDCardEvent(s_hUnit, Callback, (void*) tilt);
}

static void ThreadCleanup(Tilt *tilt, void *pClientData) {
    LT_UNUSED(tilt);
    LT_UNUSED(pClientData);
    LTThread thread = VOIDPTR_TO_LTHANDLE(pClientData);

    s_iSDCard->NoSDCardEvent(s_hUnit, Callback);
    s_iThread->Terminate(thread);
    s_iThread->WaitUntilFinished(thread, LTTime_Infinite());
    s_iThread->Destroy(thread);
    LT_GetCore()->Destroy(s_hUnit);
    s_hUnit = 0;
}

static void TestSDCardHotplug(Tilt * tilt) {
    s_hUnit = s_SDCard->CreateDeviceUnitHandle(0);
    TILT_EXPECT_TRUE(tilt, s_hUnit, "Expect unit handle");
    s_iSDCard = lt_gethandleinterface(ILTSDCardDeviceUnit, s_hUnit);
    TILT_EXPECT_TRUE(tilt, s_iSDCard, "Expected interface exists");

    LTThread thread =  LT_GetCore()->CreateThread("SDCardEvents");
    s_iThread->SetStackSize(thread, 2048);
    s_iThread->Start(thread, NULL, NULL);
    s_iThread->QueueTaskProc(thread, RegisterCallback, NULL, (void*)tilt);

    TILT_INFO(tilt, "Awaiting SD card plug/unplug");
    s_engine->API->DeferTestCompletion(s_engine, LTTime_Seconds(30), ThreadCleanup, LTHANDLE_TO_VOIDPTR(thread));
}


static void BeforeAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    s_SDCard = lt_openlibrary(LTDeviceSDCard);
    TILT_EXPECT_TRUE(tilt, s_SDCard != NULL, "Cannot open LTDeviceSDCard");
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    if (s_hUnit) {
        LT_GetCore()->Destroy(s_hUnit);
        s_hUnit = 0;
    }
    lt_closelibrary(s_SDCard);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

static const TiltEngineTest s_tests[] = {
    { TestManageSDCard,  "TestManageSDCard",  NULL, 0                            },
    { TestSDCardHotplug, "TestSDCardHotplug", NULL, kTiltEngineFlag_NoAutomation },
};

static int UnitTestLTDeviceSDCardImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceSDCardImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceSDCardImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceSDCard, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceSDCard, UnitTestLTDeviceSDCardImpl_Run, 1536) LTLIBRARY_DEFINITION;
