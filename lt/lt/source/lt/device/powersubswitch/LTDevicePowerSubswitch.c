/*******************************************************************************
 * lt/device/powersubswitch/LTDevicePowerSubswitch.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/device/powersubswitch/LTDevicePowerSubswitch.h>
#include <lt/driver/powersubswitch/LTDriverPowerSubswitch.h>
#include <lt/device/config/LTDeviceKonfig.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>

/* _____________
  / #defines  */
#define LTDPSS_MAX_UNIT_NAME_SIZE       16
#define LTDPSS_PREVENT_SLEEP_VARNAME    "preventSleepWhenPoweredOn"

DEFINE_LTLOG_SECTION("dev.powersubswitch");
#define USE_DLOG 1
#if USE_DLOG
#define DLOG LTLOG
#else
#define DLOG LTLOG_NULL
#endif

/* _________________
  / private types */
typedef struct DriverRecord {
    LTTime                  lastPoweredOffTime;
    LTDriverPowerSubswitch *driver;
    u32                     refCountClients;
    u32                     refCountPoweredOn;
    char                    unitName[LTDPSS_MAX_UNIT_NAME_SIZE];
    u32                     disallowanceGrant;
    bool                    takeDisallowanceGrant;
} DriverRecord;

/* _____________________
  / static variables  */
LTMutex *s_mutex;
LTArray *s_driverRecords;

/* _______________________
  / object impl struct  */
typedef_LTObjectImpl(LTDevicePowerSubswitch, LTDevicePowerSubswitchImpl) {
    DriverRecord *record;
    LTTime timeSwitchedOn;  /* for efficient implementation of TurnAllSwitchesOff */
} LTOBJECT_API;

/* _________________________
  / forward declarations  */
static void LibFini(void);
static bool LTDevicePowerSubswitchImpl_IsSwitchOn(LTDevicePowerSubswitchImpl *subswitch);

/* ______________________
  / Lib init and fini  */
static bool LibInit(void) {
    DLOG("libinit", NULL);
    do {
        if (NULL == (s_mutex = lt_createobject(LTMutex))) break;
        if (NULL == (s_driverRecords = lt_createobject_typed(LTArray, List))) break;
        s_driverRecords->API->TuneAllocation(s_driverRecords, 1, 2);
        return true;
    } while(false);

    LibFini();

    return false;
}

static void LibFini(void) {
    DLOG("libfini", NULL);

    lt_destroyobject(s_mutex);
    s_mutex = NULL;

    if (s_driverRecords) {
        u32 nCount = s_driverRecords->API->GetCount(s_driverRecords);
        for (u32 i = 0; i < nCount; i++) {
            DriverRecord *record = (DriverRecord*)s_driverRecords->API->Get(s_driverRecords, i, NULL);
            lt_destroyobject(record->driver);
            lt_free(record);
        }
        lt_destroyobject(s_driverRecords);
        s_driverRecords = NULL;
    }
}

/* _____________________
  / helper functions  */
static void RemoveRecordInternal(DriverRecord *recordToRemove) {
    /* Find, remove, and free matching record and driver from list */
    if (recordToRemove) {
        u32 nCount = s_driverRecords->API->GetCount(s_driverRecords);
        for (u32 i = 0; i < nCount; i++) {
            DriverRecord *record = (DriverRecord*)s_driverRecords->API->Get(s_driverRecords, i, NULL);
            if (record == recordToRemove) {
                s_driverRecords->API->Remove(s_driverRecords, i);
                lt_destroyobject(record->driver);
                /* when a switch record is removed; we leave whatever power state the device is in alone, however
                   we release the disallowance grant, otherwise sleep will never occur forever after */
                if (record->disallowanceGrant) lt_reallowsleepmode(record->disallowanceGrant);
                lt_free(record);
            }
        }
    }
}

/* ________________________
  / object constructors  */
static bool LTDevicePowerSubswitchImpl_ConstructObject(LTDevicePowerSubswitchImpl *subswitch) {
    LT_UNUSED(subswitch);
    return true;
}

static void LTDevicePowerSubswitchImpl_DestructObject(LTDevicePowerSubswitchImpl *subswitch) {
    s_mutex->API->Lock(s_mutex);
    DriverRecord *record = subswitch->record;
    if (record) {
        if (LTDevicePowerSubswitchImpl_IsSwitchOn(subswitch) && record->refCountPoweredOn) record->refCountPoweredOn--;
        if (0 == --record->refCountClients) {
            RemoveRecordInternal(record);
        }
    }
    s_mutex->API->Unlock(s_mutex);
}

/* ______________________________
  / public api implementation  */
static void LTDevicePowerSubswitchImpl_SetUnitName(LTDevicePowerSubswitchImpl *subswitch, const char *unitName) {
    if (NULL == unitName || 0 == *unitName) return;
    LTDeviceKonfig *konfig = NULL;
    s_mutex->API->Lock(s_mutex);
    if (NULL != subswitch->record) goto done; /* only set once */

    /* manually find the record  */
    DriverRecord *record = NULL;
    u32 nCount = s_driverRecords->API->GetCount(s_driverRecords);
    u32 i = 0;
    for (; i < nCount; i++) {
        record = (DriverRecord*)s_driverRecords->API->Get(s_driverRecords, i, NULL);
        if (0 == lt_strncmp(unitName, record->unitName, sizeof(record->unitName) - 1)) break;
    }
    if (i < nCount) {
        /* found it */
        subswitch->record = record;
        record->refCountClients++;
        goto done;
    }

    /* creating a new record; create the driver object */
    konfig = lt_createobject(LTDeviceKonfig);
    u32 nDeviceIndex = 0, nUnitIndex = 0;
    LTDriverPowerSubswitch *driver = NULL;
    if (konfig->API->GetDeviceUnitIndices(konfig, "LTDevicePowerSubswitch", unitName, &nDeviceIndex, &nUnitIndex)) {
        const char * driverName = konfig->API->GetDeviceUnitDriver(konfig, nDeviceIndex, nUnitIndex);
        if (driverName) driver = (LTDriverPowerSubswitch *)lt_createobject_named("LTDriverPowerSubswitch", driverName);
    }
    if (NULL == driver) goto done;
    driver->API->SetUnitName(driver, unitName);

    /* create the record */
    record = lt_malloc(sizeof(DriverRecord));
    if (NULL == record) {
        lt_destroyobject(driver);
        goto done;
    }
    s_driverRecords->API->Append(s_driverRecords, record);

    /* fill in the record */
    record->lastPoweredOffTime = LTTime_Zero();
    record->driver = driver;
    record->refCountClients = 1;
    i = konfig->API->GetDeviceUnitConfigSectionByIndex(konfig, nDeviceIndex, nUnitIndex);
    record->takeDisallowanceGrant = (i && konfig->API->ReadInteger(konfig, i, LTDPSS_PREVENT_SLEEP_VARNAME)) ? true : false;
    LTLOG("temp.disallowance.report", "LTDevicePowerSubswitch.%s takeDisallowanceGrant = %s", unitName, record->takeDisallowanceGrant ? "true" : "false");
    if (driver->API->IsDevicePoweredOn(driver)) {
        record->refCountPoweredOn = 1;
        record->disallowanceGrant = record->takeDisallowanceGrant ? lt_disallowsleepmode() : 0;
    } else {
        record->refCountPoweredOn = 0;
    }
    lt_strncpyTerm(record->unitName, unitName, sizeof(record->unitName));

    /* fill in the subswitch */
    subswitch->record = record;
    subswitch->timeSwitchedOn = record->refCountPoweredOn ? LT_GetCore()->GetKernelTime() : LTTime_Zero();

done:
    lt_destroyobject(konfig);
    s_mutex->API->Unlock(s_mutex);
}

static const char *LTDevicePowerSubswitchImpl_GetUnitName(LTDevicePowerSubswitchImpl *subswitch) {
    DriverRecord *record = subswitch->record;
    return record->unitName;
}

static void LTDevicePowerSubswitchImpl_SwitchOn(LTDevicePowerSubswitchImpl *subswitch) {
    s_mutex->API->Lock(s_mutex);
    if (LTDevicePowerSubswitchImpl_IsSwitchOn(subswitch)) goto done;
    DriverRecord *record = subswitch->record;
    if (NULL == record) goto done;
    if (0 == record->refCountPoweredOn++) {
        record->driver->API->PowerOn(record->driver);
        record->lastPoweredOffTime = LTTime_Zero();
        record->disallowanceGrant = record->takeDisallowanceGrant ? lt_disallowsleepmode() : 0;
    }
    subswitch->timeSwitchedOn = LT_GetCore()->GetKernelTime();
done:
    s_mutex->API->Unlock(s_mutex);
}

static void LTDevicePowerSubswitchImpl_SwitchOff(LTDevicePowerSubswitchImpl *subswitch) {
    s_mutex->API->Lock(s_mutex);
    if (! LTDevicePowerSubswitchImpl_IsSwitchOn(subswitch)) goto done;
    DriverRecord *record = subswitch->record;
    if (NULL == record) goto done;
    if (0 == --record->refCountPoweredOn) {
        record->driver->API->PowerOff(record->driver);
        record->lastPoweredOffTime = LT_GetCore()->GetKernelTime();
        if (record->takeDisallowanceGrant) { lt_reallowsleepmode(record->disallowanceGrant); record->disallowanceGrant = 0; }
    }
    subswitch->timeSwitchedOn = LTTime_Zero();
done:
    s_mutex->API->Unlock(s_mutex);
}

static bool LTDevicePowerSubswitchImpl_SwitchOnIfDevicePowered(LTDevicePowerSubswitchImpl *subswitch) {
    bool bIsOn = false;
    s_mutex->API->Lock(s_mutex);
    if (true == (bIsOn = (subswitch->record && subswitch->record->refCountPoweredOn))) {
        if (! LTDevicePowerSubswitchImpl_IsSwitchOn(subswitch)) {
            subswitch->record->refCountPoweredOn++;
            subswitch->timeSwitchedOn = LT_GetCore()->GetKernelTime();
        }
    }
    s_mutex->API->Unlock(s_mutex);
    return bIsOn;
}

static bool LTDevicePowerSubswitchImpl_IsSwitchOn(LTDevicePowerSubswitchImpl *subswitch) {
    bool bResult = false;
    s_mutex->API->Lock(s_mutex);
    DriverRecord *record = subswitch->record;
    if (record) {
        if (LTTime_IsLessThan(subswitch->timeSwitchedOn, record->lastPoweredOffTime)) {
            subswitch->timeSwitchedOn = LTTime_Zero();
        }
        bResult = LTTime_IsZero(subswitch->timeSwitchedOn) ? false : true;
    }
    s_mutex->API->Unlock(s_mutex);
    return bResult;
}

static bool LTDevicePowerSubswitchImpl_IsDevicePoweredOn(LTDevicePowerSubswitchImpl *subswitch) {
    s_mutex->API->Lock(s_mutex);
    DriverRecord *record = subswitch->record;
    bool bResult = record && record->driver->API->IsDevicePoweredOn(record->driver);
    s_mutex->API->Unlock(s_mutex);
    return bResult;
}

static u32 LTDevicePowerSubswitchImpl_HowManySwitchesAreOn(LTDevicePowerSubswitchImpl *subswitch) {
    s_mutex->API->Lock(s_mutex);
    DriverRecord *record = subswitch->record;
    u32 nSwitchesOn = record ? record->refCountPoweredOn : 0;
    s_mutex->API->Unlock(s_mutex);
    return nSwitchesOn;
}

static void LTDevicePowerSubswitchImpl_TurnAllSwitchesOff(LTDevicePowerSubswitchImpl *subswitch) {
    s_mutex->API->Lock(s_mutex);
    DriverRecord *record = subswitch->record;
    if (record && record->refCountPoweredOn) {
        record->refCountPoweredOn = 0;
        record->driver->API->PowerOff(record->driver);
        record->lastPoweredOffTime = LT_GetCore()->GetKernelTime();
        subswitch->timeSwitchedOn = LTTime_Zero();
        if (record->takeDisallowanceGrant) { lt_reallowsleepmode(record->disallowanceGrant); record->disallowanceGrant = 0; }
    }
    s_mutex->API->Unlock(s_mutex);
}

define_LTObjectImplPublic(LTDevicePowerSubswitch, LTDevicePowerSubswitchImpl,
    SetUnitName,
    GetUnitName,
    SwitchOn,
    SwitchOff,
    SwitchOnIfDevicePowered,
    IsSwitchOn,
    IsDevicePoweredOn,
    HowManySwitchesAreOn,
    TurnAllSwitchesOff);

define_LTObjectLibrary(1, LibInit, LibFini);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Mar-25   augustus    created
 *  06-May-25   augustus    added SwitchOnIfDevicePowered
 */
