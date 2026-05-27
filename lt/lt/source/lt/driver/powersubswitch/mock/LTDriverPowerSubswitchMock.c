/*******************************************************************************
 * platforms/linux/source/linux/driver/powersubswitch/mock/LTDriverPowerSubswitchMock.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/driver/powersubswitch/LTDriverPowerSubswitch.h>
#include <lt/core/LTCore.h>

/* _____________
  / #defines  */
#define POWER_SUBSWITCH_UNITNAME "mock.subswitch" /* we only support 1 */

DEFINE_LTLOG_SECTION("mock.subswitch");
#define USE_DLOG 1
#if USE_DLOG
#define DLOG LTLOG
#else
#define DLOG LTLOG_NULL
#endif

/* _______________________
  / object impl struct  */
typedef_LTObjectImpl(LTDriverPowerSubswitch, LTDriverPowerSubswitchMock) {
    const char * unitName;
    bool powered;
} LTOBJECT_API;

/* ______________________
  / Lib init and fini  */
static bool LibInit(void) {
    DLOG("libinit", NULL);
    return true;
}

static void LibFini(void) {
    DLOG("libfini", NULL);
}

/* ________________________
  / object constructors  */
static bool LTDriverPowerSubswitchMock_ConstructObject(LTDriverPowerSubswitchMock *subswitch) {
    LT_UNUSED(subswitch);
    DLOG("constructobject", NULL);
    return true;
}

static void LTDriverPowerSubswitchMock_DestructObject(LTDriverPowerSubswitchMock *subswitch) {
    LT_UNUSED(subswitch);
    DLOG("destructobject", NULL);
}

/* ______________________________
  / public api implementation  */
static void LTDriverPowerSubswitchMock_SetUnitName(LTDriverPowerSubswitchMock *subswitch, const char *unitName) {
    /* this driver object only supports one POWER_SUBSWITCH_UNITNAME as defined here; just sanity check it */
    if (subswitch->unitName) {
        DLOG("setunitname", "unit name already set to \"%s\", not set", subswitch->unitName);
    }
    else {
        if (unitName && (0 == lt_strcmp(POWER_SUBSWITCH_UNITNAME, unitName))) {
            subswitch->unitName = POWER_SUBSWITCH_UNITNAME;
            DLOG("setunitname", "unit name set to \"%s\"", subswitch->unitName);
        }
        else {
            DLOG("setunitname", "unknown unit name \"%s\", not set", unitName ? subswitch->unitName : NULL);
        }
    }
}

static const char * LTDriverPowerSubswitchMock_GetUnitName(LTDriverPowerSubswitchMock *subswitch) {
    DLOG("getunitname", "unit name is: %s", subswitch->unitName ? subswitch->unitName : "<unset>");
    return subswitch->unitName ? subswitch->unitName : "";
}

static void LTDriverPowerSubswitchMock_PowerOn(LTDriverPowerSubswitchMock *subswitch) {
    DLOG("poweron", "power changed from %s to on", subswitch->powered ? "on" : "off");
    subswitch->powered = true;
}

static void LTDriverPowerSubswitchMock_PowerOff(LTDriverPowerSubswitchMock *subswitch) {
    DLOG("poweroff", "power changed from %s to off", subswitch->powered ? "on" : "off");
    subswitch->powered = false;
}

static bool LTDriverPowerSubswitchMock_IsDevicePoweredOn(LTDriverPowerSubswitchMock *subswitch) {
    DLOG("isdevicepoweredon", "power is %s", subswitch->powered ? "on" : "off");
    return subswitch->powered;
}

define_LTObjectImplPublic(LTDriverPowerSubswitch, LTDriverPowerSubswitchMock,
    SetUnitName,
    GetUnitName,
    PowerOn,
    PowerOff,
    IsDevicePoweredOn);

define_LTObjectLibrary(1, LibInit, LibFini);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-Mar-25   augustus    created
 */
