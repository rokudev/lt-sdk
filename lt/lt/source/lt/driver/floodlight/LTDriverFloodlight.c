/*******************************************************************************
 * lt/source/lt/driver/floodlight/LTDriverFloodlight.c
 *
 * LT Driver Library for Floodlight light
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
/** @file LTDriverFloodlight.c Implementation of Floodlight Light driver
 */

#include <lt/core/LTCore.h>
#include <lt/device/floodlight/LTDeviceFloodlight.h>
#include <lt/device/flunit/LTDeviceFLUnit.h>

DEFINE_LTLOG_SECTION("drv.floodlight");

static LTDeviceFLUnit *s_FlUnit;
static LTHandle        s_handle;
/**
 * @brief Set-params : object brightness | flash frequency    | accommodation time
 *        Get-params : object brightness | current brightness | flash frequency
 */
typedef struct {
    u8 objectBrightness;
    union {
        u8 setflashFrequency; /*set-param*/
        u8 currentBrightnes;  /*get-param*/
    };
    union {
        u8 accTime;           /*set-param*/
        u8 getflashFrequency; /*get-param*/
    };
} LightParamters;

static LightParamters s_Light;

enum {
    kMaxFlashFrequencyHz         = 10,
    kMaxAccommodationTimeSeconds = 100,
    kMaxBrightness               = 255,
    kScaledMinBrightness         = 15,  // min lux of light for good visibility
    kScaledMaxBrightness         = kMaxBrightness - kScaledMinBrightness,
    kPercentConversionFactor     = 99,  // 1 - 100
    kPercentRoundingOff          = kPercentConversionFactor/2,
    kBrightnessRoundingOff       = kScaledMaxBrightness/2,
};

static bool LTDriverFloodlightImpl_SetBrightness(u32 brightness, LightBrightnessType type) {
    LightParamters lightData = s_Light;
    u32 value = 0;
    if (brightness) {
        value = (u32)((((brightness - 1) * kScaledMaxBrightness) + kPercentRoundingOff) / kPercentConversionFactor) + (u32)(kScaledMinBrightness);
    }
    if (value > kMaxBrightness) {
        LTLOG_YELLOWALERT("set.bright.range", "max brightness range is 100");
        return false;
    }
    switch (type) {
        case kLight_ObjectBrightness:
            lightData.objectBrightness  = value;
            lightData.setflashFrequency = 0;   // turn off flashing
            lightData.accTime           = 0;
            break;
        case kLight_CurrentBrightness:
            break;
        default:
            LTLOG_YELLOWALERT("set.bright.type", "unknown LightBrightnessType");
            break;
    }
    if (!s_FlUnit->SetCommand(&s_handle, LT_FL_CMD_SET_BRIGHTNESS_PARAMETER, (u8 *)&lightData, sizeof(LightParamters))) {
        LTLOG_YELLOWALERT("set.bright.fail", " Failed to set Brightness ");
        return false;
    }
    s_Light.objectBrightness = value;
    return true;
}

static bool LTDriverFloodlightImpl_GetBrightness(u32 *brightness, LightBrightnessType type) {
    LightParamters lightData;

    if (!brightness) return false;
    if (!s_FlUnit->GetCommand(&s_handle, LT_FL_CMD_GET_BRIGHTNESS_PARAMETER, (u8 *)&lightData, sizeof(LightParamters))) {
        LTLOG_YELLOWALERT("get.bright.fail", " Failed to get Brightness ");
        return false;
    }
    switch (type) {
        case kLight_ObjectBrightness:
            if (lightData.objectBrightness) {
                *brightness = ((((lightData.objectBrightness - kScaledMinBrightness) * kPercentConversionFactor) + kBrightnessRoundingOff) / kScaledMaxBrightness) + 1;
            } else {
                *brightness = 0;
            }
            break;
        case kLight_CurrentBrightness:
            if (lightData.currentBrightnes) {
                *brightness = ((((lightData.currentBrightnes - kScaledMinBrightness) * kPercentConversionFactor) + kBrightnessRoundingOff) / kScaledMaxBrightness) + 1;
            } else {
                *brightness = 0;
            }
            break;
        default:
            LTLOG_YELLOWALERT("get.bright.type", "unknown LightBrightnessType");
            break;
    }
    return true;
}

static bool LTDriverFloodlightImpl_SetFlashFrequency(u32 flashFrequency) {
    LightParamters lightData = s_Light;

    if (flashFrequency > kMaxFlashFrequencyHz) {
        LTLOG_YELLOWALERT("set.flash.range", "maximum flash frequency is %dHz", kMaxFlashFrequencyHz);
        return false;
    }
    lightData.setflashFrequency = flashFrequency;
    lightData.objectBrightness  = 0;
    lightData.accTime           = 0;

    if (!s_FlUnit->SetCommand(&s_handle, LT_FL_CMD_SET_BRIGHTNESS_PARAMETER, (u8 *)&lightData, sizeof(LightParamters))) {
        LTLOG_YELLOWALERT("set.flash.fail", " Failed to set flash frequency");
        return false;
    }
    s_Light.setflashFrequency = (u8)flashFrequency;
    return true;
}

static bool LTDriverFloodlightImpl_GetFlashFrequency(u32 *flashFrequency) {
    LightParamters lightData;
    if (!flashFrequency) return false;

    if (!s_FlUnit->GetCommand(&s_handle, LT_FL_CMD_GET_BRIGHTNESS_PARAMETER, (u8 *)&lightData, sizeof(LightParamters))) {
        LTLOG_YELLOWALERT("get.flash.fail", " Failed to get flash frequency");
        return false;
    }
    *flashFrequency = (u32)lightData.getflashFrequency;
    return true;
}

static bool LTDriverFloodlightImpl_SetAccomodationTime(u32 accomodationTime) {
    LightParamters lightData = s_Light;
    if (accomodationTime > kMaxAccommodationTimeSeconds) {
        LTLOG_YELLOWALERT("set.time.range", "Max accommodation time is %d sec", kMaxAccommodationTimeSeconds);
        return false;
    }
    lightData.accTime = accomodationTime;
    lightData.objectBrightness = 0;  // Dont turn the light on
    lightData.setflashFrequency = 0;

    if (!s_FlUnit->SetCommand(&s_handle, LT_FL_CMD_SET_BRIGHTNESS_PARAMETER, (u8 *)&lightData, sizeof(LightParamters))) {
        LTLOG_YELLOWALERT("set.time.fail", " Failed to set light accommodation time");
        return false;
    }
    s_Light.accTime = accomodationTime;
    return true;
}

static void LTDriverFloodlightImpl_GetAccomodationTime(u32 *accomodationTime) {
    *accomodationTime = s_Light.accTime;
}

static void LTDriverFloodlightImpl_LibFini(void) {
    lt_closelibrary(s_FlUnit);
    lt_destroyhandle(s_handle);
}

static bool LTDriverFloodlightImpl_LibInit(void) {
    s_Light.objectBrightness = kMaxBrightness;

    do {
        if (!(s_FlUnit = lt_openlibrary(LTDeviceFLUnit))) {
            LTLOG_YELLOWALERT("init.object", "Error creating object LTDeviceFLUnit");
            break;
        }
        if (!(s_handle = s_FlUnit->CreateDeviceUnitHandle(0))) {
            LTLOG_YELLOWALERT("f.handle", "Error creating device unit handle");
            break;
        }
        if(!s_FlUnit->IsMounted(&s_handle)) {
            LTLOG_YELLOWALERT("f.mounted", "Floodlight device not found");
            break;
        }
        if (!s_FlUnit->AuthenticateDevice(&s_handle)) {
            LTLOG_YELLOWALERT("init.auth", "Failed authenticating FL unit");
            break;
        }
        return true;
    } while (0);

    LTDriverFloodlightImpl_LibFini();
    return false;
}

/*___________________________________________________
 / DriverLight library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverFloodlight, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverFloodlight) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverFloodlight) {
    .GetBrightness       = &LTDriverFloodlightImpl_GetBrightness,
    .SetBrightness       = &LTDriverFloodlightImpl_SetBrightness,
    .GetFlashFrequency   = &LTDriverFloodlightImpl_GetFlashFrequency,
    .SetFlashFrequency   = &LTDriverFloodlightImpl_SetFlashFrequency,
    .SetAccomodationTime = &LTDriverFloodlightImpl_SetAccomodationTime,
    .GetAccomodationTime = &LTDriverFloodlightImpl_GetAccomodationTime,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverFloodlight, (ILTDriverFloodlight));
