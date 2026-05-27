/*******************************************************************************
 * lt/media/cameraexposure/LTCameraExposure.c
 *
 * LT Object for calculating camera exposure parameters based on ambient light
 * Implemented as a Singleton object where state is shared across all instances.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/media/cameraexposure/LTCameraExposure.h>
#include <lt/device/media/LTDeviceMedia.h>

DEFINE_LTLOG_SECTION("cam.exp");

#define DO_DLOG 0
#if DO_DLOG
    #define DLOG LTLOG
#else
    #define DLOG LTLOG_LOGNULL
#endif


/*******************************************************************************
 * Static Variables
 ******************************************************************************/
static IlluminanceValue      s_currentLightValue = 0;

static const struct {
    IlluminanceValue ambientLight;
    LTCameraExposureParams params;
} s_exposureLookupTable[] = {
    /* {ambient light reading in milli Lux, {exposureTime, analogGain, digitalGain, ispDGain, requiresNightVision, artificialLight}}
     *
     * Note that the requiresNightVision and artificialLight parameters are not used with the lookup table.
     * However as they are part of the struct they need to be initialised or the compiler complains.
     * In particular (other than suggestive comments below) there is not strict correlation between the
     * ALS values in this table and the threshold at which a product will switch from day to night vision mode.
     */
    {0,        {3366, 14828, 256, 2048, false, false}},    /* The darkest/lowest light reading from the sensor during lab testing  (0 milli Lux) */
    {80,       {3366, 14828, 256, 266, false, false}},     /* The Dark threshold is (approximately) around this level */
    {90,       {3366, 14766, 256, 256, false, false}},
    {100,      {3366, 13854, 256, 256, false, false}},
    {110,      {3366, 12727, 256, 256, false, false}},
    {120,      {3366, 10187, 256, 256, false, false}},
    {130,      {3366, 9062,  256, 256, false, false}},
    {140,      {3366, 8502,  256, 256, false, false}},
    {150,      {3366, 8071,  256, 256, false, false}},
    {160,      {3366, 7547,  256, 256, false, false}},     /* The Dusk threshold is (approximately) around this level */
    {170,      {3366, 7138,  256, 256, false, false}},
    {180,      {3366, 6837,  256, 256, false, false}},
    {190,      {3366, 6437,  256, 256, false, false}},
    {200,      {3366, 5826,  256, 256, false, false}},
    {210,      {3366, 5563,  256, 256, false, false}},
    {220,      {3366, 5093,  256, 256, false, false}},
    {230,      {3366, 4944,  256, 256, false, false}},
    {240,      {3366, 4751,  256, 256, false, false}},
    {320,      {3366, 3700,  256, 256, false, false}},
    {400,      {3366, 2603,  256, 256, false, false}},
    {490,      {3366, 2136,  256, 256, false, false}},
    {950,      {3366, 1098,  256, 256, false, false}},
    {1360,     {3366, 810,   256, 256, false, false}},
    {2040,     {3366, 541,   256, 256, false, false}},
    {2800,     {3366, 378,   256, 256, false, false}},
    {11360,    {1122, 295,   256, 256, false, false}},
    {18400,    {561,  357,   256, 256, false, false}},
    {18560,    {561,  275,   256, 256, false, false}},
    {23120,    {483,  256,   256, 256, false, false}},
    {41120,    {270,  256,   256, 256, false, false}},
    {56400,    {199,  257,   256, 256, false, false}},
    {71840,    {156,  256,   256, 256, false, false}},
    {80480,    {138,  257,   256, 256, false, false}},
    {130880,   {84,   258,   256, 256, false, false}},
    {188480,   {60,   256,   256, 256, false, false}},
    {256960,   {43,   257,   256, 256, false, false}},
    {410240,   {27,   256,   256, 256, false, false}},
    {670720,   {16,   262,   256, 256, false, false}},
    {1010880,  {10,   272,   256, 256, false, false}},
    {1474560,  {7,    260,   256, 256, false, false}},     /* The brightest/highest light reading from the sensor during lab testing  (1,474,560 milli Lux) */
    {83865600, {7,    256,   256, 256, false, false}}      /* This value is the theoretical maximum light reading when using the OPT3004 Light Sensor */
};
static const u32 kLookupTableSize = sizeof(s_exposureLookupTable) / sizeof(s_exposureLookupTable[0]);
static bool s_artificialLight;
static bool s_belowDuskThreshold;
static bool s_belowDarkThreshold;
static LTMediaNightVisionMode s_nightVisionMode;
static LTMediaNightVisionCondition s_nightVisionConditions;


/*******************************************************************************
 * Camera Exposure Implementation Structure
 ******************************************************************************/
typedef_LTObjectImpl(LTCameraExposure, LTCameraExposureImpl) {
}
LTOBJECT_API;

/*******************************************************************************
 * Private Functions
 ******************************************************************************/

LT_INLINE u32 InterpolateExposureParam(u32 param1, u32 param2, s64 factor) {
    return lt_abs(((s32)((param1) + ((factor * ((s64)(param2) - (param1))) >> 16))));
}

/**
 * Calculate exposure parameters based on ambient light value
 * Uses linear interpolation between table entries
 *
 * @param lightValue light sensor reading
 * @param pParams pointer to store the exposure parameters
 */
static void CalculateExposureFromLookupTable(IlluminanceValue lightValue, LTCameraExposureParams *pParams) {
    if (!pParams) {
        // By design, can't fail, so no need to report errors. But let's avoid NULL pointer dereference anyway
        return;
    }

    // Find appropriate segment in the lookup table
    u32 i;
    for (i = 0; i < kLookupTableSize - 1; i++) {
        if (lightValue < s_exposureLookupTable[i + 1].ambientLight) {
            break;
        }
    }
    // If we're at the last entry or exact match, return that value
    if (i == kLookupTableSize - 1 || lightValue == s_exposureLookupTable[i].ambientLight) {
        *pParams = s_exposureLookupTable[i].params;  // Single line assignment
        return;
    }
    // Linear interpolation between adjacent table entries
    s32 light1 = s_exposureLookupTable[i].ambientLight;
    s32 light2 = s_exposureLookupTable[i + 1].ambientLight;
    // Calculate interpolation factor (0.0 to 1.0 represented as fixed point)
    s64 factor = (((s64)(lightValue - light1)) << 16) / (light2 - light1);

    const LTCameraExposureParams *pParams1 = &s_exposureLookupTable[i].params;
    const LTCameraExposureParams *pParams2 = &s_exposureLookupTable[i + 1].params;
    pParams->exposureTime = InterpolateExposureParam(pParams1->exposureTime, pParams2->exposureTime, factor);
    pParams->analogGain   = InterpolateExposureParam(pParams1->analogGain, pParams2->analogGain, factor);
    pParams->digitalGain  = InterpolateExposureParam(pParams1->digitalGain, pParams2->digitalGain, factor);
    pParams->ispDGain     = InterpolateExposureParam(pParams1->ispDGain, pParams2->ispDGain, factor);
}

/*******************************************************************************
 * Public API Implementation
 ******************************************************************************/

static void LTCameraExposureImpl_SetAmbientLightLevel(IlluminanceValue lightValue) {
    s_currentLightValue = lightValue;
    DLOG("ambient.level", "%lu", LT_Pu32(lightValue));
}

static void LTCameraExposureImpl_SetLightingConditionFlags(bool artificialLight, bool belowDuskThreshold, bool belowDarkThreshold) {
    s_artificialLight = artificialLight;
    s_belowDuskThreshold = belowDuskThreshold;
    s_belowDarkThreshold = belowDarkThreshold;
    DLOG("light.cond", "Artificial: %s, Dusk: %s, Dark: %s", s_artificialLight ? "Yes" : "No", s_belowDuskThreshold ? "Yes" : "No", s_belowDarkThreshold ? "Yes" : "No");
}

static void LTCameraExposureImpl_SetNightVisionMode(LTMediaNightVisionMode nightVisionMode, LTMediaNightVisionCondition nightVisionConditions) {
    s_nightVisionMode = nightVisionMode;
    s_nightVisionConditions = nightVisionConditions;
    DLOG("night.vision", "Mode: %d, Conditions: %d", s_nightVisionMode, s_nightVisionConditions);
}

static LTMediaNightVisionMode LTCameraExposureImpl_GetNightVisionMode(void) {
    return s_nightVisionMode;
}

static LTCameraExposureResult LTCameraExposureImpl_RetrieveExposureParams(LTCameraExposureParams *pParams) {
    // Calculate exposure parameters from stored light value
    if (!pParams) return kLTCameraExposure_Result_Failure;

    CalculateExposureFromLookupTable(s_currentLightValue, pParams);

    // If night vision is forced, or if it's auto and below relevant threshold, set night vision flag
    if (s_nightVisionMode == kLTMediaDayNight_Force_Night ||
        (s_nightVisionMode == kLTMediaDayNight_Auto &&
            ((s_nightVisionConditions == kLTMediaNightVision_FromDusk && s_belowDuskThreshold) ||
            (s_nightVisionConditions == kLTMediaNightVision_FromDark && s_belowDarkThreshold)))) {
        pParams->requiresNightVision = true;
    } else {
        pParams->requiresNightVision = false;
    }
    pParams->artificialLight = s_artificialLight;

    return kLTCameraExposure_Result_Success;
}

/*******************************************************************************
 * Object Constructor and Destructor
 ******************************************************************************/
static bool LTCameraExposureImpl_ConstructObject(LTCameraExposureImpl *exposure) {
    LT_UNUSED(exposure);
    return true;
}

static void LTCameraExposureImpl_DestructObject(LTCameraExposureImpl *exposure) {
    LT_UNUSED(exposure);
}
/*******************************************************************************
 * Library Init/Fini Functions
 ******************************************************************************/
static bool LTCameraExposure_LibInit(void) {
    // Don't automatically initialize - let it be done on demand
    // Initialize with middle entry light value as default
    u32 defaultIndex = kLookupTableSize / 2;
    s_currentLightValue = s_exposureLookupTable[defaultIndex].ambientLight;
    return true;
}

/*******************************************************************************
 * Object API binding
 ******************************************************************************/
define_LTObjectImplPublic(LTCameraExposure, LTCameraExposureImpl,
                          SetAmbientLightLevel,
                          SetLightingConditionFlags,
                          SetNightVisionMode,
                          GetNightVisionMode,
                          RetrieveExposureParams);
/*******************************************************************************
 * Library Binding
 ******************************************************************************/
define_LTObjectLibrary(1, LTCameraExposure_LibInit, NULL);
