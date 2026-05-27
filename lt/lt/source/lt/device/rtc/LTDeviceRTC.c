/*******************************************************************************
 * lt/source/lt/device/rtc/LTDeviceRTC.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/rtc/LTDeviceRTC.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/driver/rtc/LTDriverRTC.h>

DEFINE_LTLOG_SECTION("dev.rtc");

                           /*  \|/  */
#define LTDEVICERTC_DO_DLOG    (0)  /* ALWAYS RESTORE THIS VALUE TO 0 (ZERO) BEFORE MERGING */
                           /*  /|\  */
#if     LTDEVICERTC_DO_DLOG
#define DLOG                    LTLOG
#else
#define DLOG                    LTLOG_LOGNULL
#endif

static LTDriverRTC *s_Driver = NULL;

typedef_LTObjectImpl(LTDeviceRTC, LTDeviceRTCImpl) {} LTOBJECT_API;

static bool LTDeviceRTCImpl_ConstructObject(LTDeviceRTCImpl *rtc) { LT_UNUSED(rtc); DLOG("construct", NULL); return true; }

static void LTDeviceRTCImpl_DestructObject(LTDeviceRTCImpl *rtc) { LT_UNUSED(rtc); DLOG("destruct", NULL); }

/* If at all possible (and necessary), this should not be just a pass-through.  Don't put anything in the Driver that can go here. */

static void LTDeviceRTCImpl_SetTimeUTC(LTDeviceRTCImpl *rtc, LTTime time) { LT_UNUSED(rtc); if (s_Driver) s_Driver->API->SetTimeUTC(s_Driver, time); }

static LTTime LTDeviceRTCImpl_GetTimeUTC(LTDeviceRTCImpl *rtc) { LT_UNUSED(rtc); return s_Driver ? s_Driver->API->GetTimeUTC(s_Driver) : LTTime_Zero(); }

static void LTDeviceRTCImpl_EnableAlarmInterrupt(LTDeviceRTCImpl *rtc, LTTime time, LTDeviceRTC_AlarmInterruptCallback *callback, void *clientData) { LT_UNUSED(rtc);
    if (s_Driver) s_Driver->API->EnableAlarmInterrupt(s_Driver, time, callback, clientData);
}

static void LTDeviceRTCImpl_DisableAlarmInterrupt(LTDeviceRTCImpl *rtc) { LT_UNUSED(rtc); if (s_Driver) s_Driver->API->DisableAlarmInterrupt(s_Driver); }

define_LTObjectImplPublic(LTDeviceRTC, LTDeviceRTCImpl, SetTimeUTC, GetTimeUTC, EnableAlarmInterrupt, DisableAlarmInterrupt);

/***************************************************************************************
 * Library startup and shutdown:                                                      */

static void LibFini(void) {
    DLOG("fini", NULL);
    if (s_Driver) { lt_destroyobject(s_Driver); s_Driver = NULL; }
}

static bool LibInit(void) {
    bool bOK = false;
    DLOG("init", NULL);
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!deviceConfig) return false;
    do {
        const char *driverLibraryName = deviceConfig->GetDriverAt("LTDeviceRTC", 0);
        if (!driverLibraryName) {
            LTLOG_YELLOWALERT("driver.no", NULL);
            break;
        }
        if (!(s_Driver = (LTDriverRTC *)lt_createobject_named("LTDriverRTC", driverLibraryName))) {
            LTLOG_YELLOWALERT("driver.fail", NULL);
            break;
        }
        bOK = true;
    } while (0);
    lt_closelibrary(deviceConfig);
    if (!bOK) LibFini();
    return bOK;
}

define_LTObjectLibrary(1, LibInit, LibFini)

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Aug-24   constantine created
 */
