/*******************************************************************************
 * lt/source/lt/device/analogmic/LTDeviceAnalogMicImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/analogmic/LTDeviceAnalogMic.h>
#include <lt/device/config/LTDeviceConfig.h>

/*******************************************************************************
 * Access to the LTDriverAnalogMic library and its secondary interface, through
 * which the Device accesses analogmic-related functions:                      */
static LTDriverLibrary         *s_pLibAnalogMicDriver    = NULL;
static ILTDriverAnalogMic       *s_pILTDriverAnalogMic    = NULL;

/*******************************************************************************
 * Unload the Driver library:                                                 */
static void ShutDownAnalogMicDriver(void) {
    if (s_pLibAnalogMicDriver) LT_GetCore()->CloseLibrary((LTLibrary *)s_pLibAnalogMicDriver);
    s_pLibAnalogMicDriver = NULL;
    s_pILTDriverAnalogMic = NULL;
}

/*******************************************************************************
 * Library startup and shutdown:                                              */
static bool LTDeviceAnalogMicImpl_LibInit(void) {
    s_pLibAnalogMicDriver = LTDeviceConfig_OpenDriverLibForDevice("LTDeviceAnalogMic", 0);
    if (!s_pLibAnalogMicDriver) { ShutDownAnalogMicDriver(); return false; }
    s_pILTDriverAnalogMic = lt_getlibraryinterface(ILTDriverAnalogMic, s_pLibAnalogMicDriver);
    if (!s_pILTDriverAnalogMic) { ShutDownAnalogMicDriver(); return false; }
    return true;
}

/*********************************************************************************************************************************
 * LTDeviceAnalogMic does not use Device Units, as there is typically only one  analogmic in play per core.  If desired, the
 * library can easily convert to the Device Unit model:                                                                          */
static u32 LTDeviceAnalogMicImpl_GetNumDeviceUnits(void) { return 0; }
static LTDeviceUnit LTDeviceAnalogMicImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) { LT_UNUSED(nDeviceUnitNumber); return 0; }


static void LTDeviceAnalogMicImpl_LibFini(void)              { ShutDownAnalogMicDriver(); }

static bool LTDeviceAnalogMicImpl_StartCap(LTDeviceAMicAudioCallback *fp) { return s_pILTDriverAnalogMic ? s_pILTDriverAnalogMic->StartCap(fp) : false; }
static bool LTDeviceAnalogMicImpl_StopCap(void)              { return s_pILTDriverAnalogMic ? s_pILTDriverAnalogMic->StopCap() : false; }
static void LTDeviceAnalogMicImpl_SetGain(int g)             { if (s_pILTDriverAnalogMic) s_pILTDriverAnalogMic->SetGain(g); }
static int  LTDeviceAnalogMicImpl_GetGain(void)              { return s_pILTDriverAnalogMic ? s_pILTDriverAnalogMic->GetGain() : 0; }
static bool LTDeviceAnalogMicImpl_SetBuffSize(int sz, s16 *buf) { return s_pILTDriverAnalogMic ? s_pILTDriverAnalogMic->SetBuffSize(sz, buf) : false; }

define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceAnalogMic)
    .StartCap         = LTDeviceAnalogMicImpl_StartCap,
    .StopCap          = LTDeviceAnalogMicImpl_StopCap,
    .SetGain          = LTDeviceAnalogMicImpl_SetGain,
    .GetGain          = LTDeviceAnalogMicImpl_GetGain,
    .SetBuffSize      = LTDeviceAnalogMicImpl_SetBuffSize,
LTLIBRARY_DEFINITION
