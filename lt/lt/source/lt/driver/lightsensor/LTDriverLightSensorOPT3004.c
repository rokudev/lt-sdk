/*******************************************************************************
 * lt/source/lt/driver/lightsensor/LTDriverLightSensorOPT3004.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for the OPT3004 ambient light sensor
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/lightsensor/LTDeviceLightSensor.h>

DEFINE_LTLOG_SECTION("drv.lightsensor.opt3004");

#define LIGHTSENSOROPT3004DRIVER_DO_DLOG 0
#if     LIGHTSENSOROPT3004DRIVER_DO_DLOG
#define DLOG                               LTLOG
#else
#define DLOG                               LTLOG_LOGNULL
#endif


typedef struct {
    struct {
        u32         i2cBusIndex;
        u16         i2cAddress;
    } cfg;
    LTDeviceUnit    hI2cBus;
} OPT3004_Instance;

/* Container for all the sensor group instances */
typedef struct DeviceUnits {
    OPT3004_Instance *pDeviceUnits; /* Pointer into the heap where the Device Units are stored (as an array) */
    u32               nNumDeviceUnits;  /* How many Device Units this Driver supplies                            */
} DeviceUnits;

static DeviceUnits s_DeviceUnits;

static LTDeviceI2C *s_libI2C;

/********************************************************************************************************************************
 * Driver interface implementation
 */

typedef enum {
    kOPT3004RegAddr_Result    = 0x00,
    kOPT3004RegAddr_Config    = 0x01,
    kOPT3004RegAddr_LowLimit  = 0x02,
    kOPT3004RegAddr_HighLimit = 0x03,
    kOPT3004RegAddr_ManuID    = 0x7E,
    kOPT3004RegAddr_DeviceID  = 0x7F,
} OPT3004RegAddr;

enum {
    kI2CRetries      = 3,

    kOPT3004ManuID   = 0x5449,
    kOPT3004DeviceID = 0x3001,

    // Configuration register bit fields
    kOPT3004AutoFullScale       = 0xc << 12,
    kOPT3004ConversionTimeShort = 0x0 << 11,
    kOPT3004ConversionTimeLong  = 0x1 << 11,
    kOPT3004ModeContinuous      = 0x3 << 9,
    kOPT3004ModeShutdown        = 0x0 << 9,
    kOPT3004Overflow            = 0x1 << 8, // read-only
    kOPT3004ConversionReady     = 0x1 << 7, // read-only
    kOPT3004FlagHigh            = 0x1 << 6, // read-only
    kOPT3004FlagLow             = 0x1 << 5, // read-only
    kOPT3004LatchDefault        = 0x1 << 4,
    kOPT3004PolarityDefault     = 0x0 << 3,
    kOPT3004MaskExponentDefault = 0x0 << 2,
    kOPT3004FaultCountDefault   = 0x0 << 0,

    kOPT3004FastIntegrationTime = 100, // 100 ms, less precision
    kOPT3004SlowIntegrationTime = 800, // 800 ms, more precision
};

static bool ReadRegister(OPT3004_Instance *instance, OPT3004RegAddr registerAddress, u16 *value) {
    u8 cmd[1] = {
        registerAddress
    };
    u8 bigEndianValue[2];
    bool ret = false;
    u8 retries = kI2CRetries;
    while (retries-- && !ret) {
        ret = s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress,
            bigEndianValue, sizeof(bigEndianValue),
            cmd, sizeof(cmd),
            true, true,
            NULL, instance);
    }

    if (ret) {
        *value = ((u16)bigEndianValue[0]) << 8 | bigEndianValue[1];
    }
    return ret;
}

static bool WriteRegister(OPT3004_Instance *instance, OPT3004RegAddr registerAddress, u16 value) {
    u8 cmd[3] = {
        registerAddress,
        (value >> 8) & 0xFF,
        (value >> 0) & 0xFF
    };
    bool ret = false;
    u8 retries = kI2CRetries;
    while (retries-- && !ret) {
        ret = s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress,
            NULL, 0,
            cmd, sizeof(cmd),
            true, true,
            NULL, instance);
    }
    return ret;
}

static bool GetVisiblePhotoSensor(OPT3004_Instance *instance, IlluminanceValue *lightSensorData) {
    if (!instance) return false;
    if (!lightSensorData) return false;

    // No need to wait for the result in continuous mode

    // Read the result, and convert to fixed point
    u16 result;
    if (!ReadRegister(instance, kOPT3004RegAddr_Result, &result)) {
        LTLOG_YELLOWALERT("get.fail", "Failed to read photo sensor result register");
        return false;
    }
    u32 exponent = result >> 12;
    u32 mantissa = result & 0x0fff;
    // The result is in 0.01 lux units, so we need to scale it to millilux
    *lightSensorData = 10 * (1 << exponent) * mantissa;

    return true;
}

static void ShutdownSensor(OPT3004_Instance *instance) {
    if (instance->hI2cBus) {
        // Set to Shutdown mode
        u16 configReg =
            kOPT3004AutoFullScale |
            kOPT3004ConversionTimeShort |
            kOPT3004ModeShutdown |
            kOPT3004LatchDefault |
            kOPT3004PolarityDefault |
            kOPT3004MaskExponentDefault |
            kOPT3004FaultCountDefault;
        if (!WriteRegister(instance, kOPT3004RegAddr_Config, configReg)) {
            LTLOG_YELLOWALERT("sht.sensor", "Failed to shut down light sensor");
        }

        lt_destroyhandle(instance->hI2cBus);
        instance->hI2cBus = LTHANDLE_INVALID;
    }
}

static bool InitSensor(OPT3004_Instance *instance) {
    if (!(instance->hI2cBus = s_libI2C->CreateDeviceUnitHandle(instance->cfg.i2cBusIndex))) {
        LTLOG_YELLOWALERT("ins.i2c.open", "Failed to open I2C bus %lu", LT_Pu32(instance->cfg.i2cBusIndex));
        return false;
    }
    LTDeviceI2C_Configuration config;
    if (!(s_libI2C->GetDeviceConfiguration(instance->hI2cBus, &config))) {
        LTLOG_YELLOWALERT("ins.i2c.getcfg", "Failed to configure I2C bus");
        ShutdownSensor(instance);
        return false;
    }
    config.Async = false;
    config.Master = true;
    if (!(s_libI2C->SetDeviceConfiguration(instance->hI2cBus, &config))) {
        LTLOG_YELLOWALERT("ins.i2c.setcfg", "Failed to configure I2C bus");
        ShutdownSensor(instance);
        return false;
    }

    u16 id = 0;
    if (!ReadRegister(instance, kOPT3004RegAddr_ManuID, &id) || id != kOPT3004ManuID) {
        LTLOG_YELLOWALERT("ins.manuid", "I2C ManuID check failed: id=%04lx", LT_Pu32(id));
        return false;
    }
    if (!ReadRegister(instance, kOPT3004RegAddr_DeviceID, &id) || id != kOPT3004DeviceID) {
        LTLOG_YELLOWALERT("ins.devid", "I2C DeviceID check failed: id=%04lx", LT_Pu32(id));
        return false;
    }

    // Set to Automatic Full-Scale Setting Mode, Continuous conversion
    u16 configReg =
        kOPT3004AutoFullScale |
        kOPT3004ConversionTimeShort |
        kOPT3004ModeContinuous |
        kOPT3004LatchDefault |
        kOPT3004PolarityDefault |
        kOPT3004MaskExponentDefault |
        kOPT3004FaultCountDefault;
    if (!WriteRegister(instance, kOPT3004RegAddr_Config, configReg)) {
        LTLOG_YELLOWALERT("ins.conf", "Failed to write config register");
        return false;
    }
    for (int i = 0; i < 100; ++i) {
        // Wait for the sensor to be ready, should take about 1 conversion time cycle (100ms)
        if (ReadRegister(instance, kOPT3004RegAddr_Config, &configReg) && (configReg & kOPT3004ConversionReady)) {
            return true;
        }
        LT_GetCore()->GetCurrentThreadObject()->API->Sleep(LTTime_Milliseconds(10));
    }
    LTLOG_YELLOWALERT("ins.ready", "Light sensor still not ready after 1 second");

    return false;
}

/********************************************************************************************************************************
 * LTDriverLightSensorOPT3004 library root interface binding
 */

u16 LTDriverLightSensorOPT3004Impl_GetSupportedChannels(void) {
    return kLTDeviceLightSensor_Channel_Visible;
}

bool LTDriverLightSensorOPT3004Impl_GetChannelValue(u16 channel, IlluminanceValue *value) {
    if (!value) return false;

    switch (channel) {
        case kLTDeviceLightSensor_Channel_Visible:
            OPT3004_Instance *instance = &s_DeviceUnits.pDeviceUnits[0];
            return GetVisiblePhotoSensor(instance, value);
        default:
            return false;
    }
}

bool LTDriverLightSensorOPT3004Impl_SetIntegrationTime(LTTime integrationTime) {
    if (LTTime_IsLessThan(integrationTime, LTTime_Milliseconds(kOPT3004FastIntegrationTime)) ||
        LTTime_IsGreaterThan(integrationTime, LTTime_Milliseconds(kOPT3004SlowIntegrationTime))) {
        LTLOG_YELLOWALERT("err.int.time", "Integration time out of range: %lld ms", LT_Ps64(LTTime_GetMilliseconds(integrationTime)));
        return false;
    }

    // The OPT3004 only supports two integration times: 100ms and 800ms. If the caller requests
    // any value greater than 100ms then select the 800ms integration time.
    OPT3004_Instance *instance = &s_DeviceUnits.pDeviceUnits[0];
    if (!instance->hI2cBus) return false;

    u16 configReg =
        kOPT3004AutoFullScale |
        kOPT3004ModeContinuous |
        kOPT3004LatchDefault |
        kOPT3004PolarityDefault |
        kOPT3004MaskExponentDefault |
        kOPT3004FaultCountDefault;

    if (LTTime_IsEqual(integrationTime, LTTime_Milliseconds(kOPT3004FastIntegrationTime))) {
        configReg |= kOPT3004ConversionTimeShort;   // Set to 100ms integration time
    } else {
        configReg |= kOPT3004ConversionTimeLong;    // Set to 800ms integration time
    }

    if (!WriteRegister(instance, kOPT3004RegAddr_Config, configReg)) {
        LTLOG_YELLOWALERT("ins.conf", "Failed to write config register");
        return false;
    }
    return true;
}


typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverLightSensorOPT3004, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverLightSensorOPT3004) LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverLightSensor) {
    .GetSupportedChannels = LTDriverLightSensorOPT3004Impl_GetSupportedChannels,
    .GetChannelValue      = LTDriverLightSensorOPT3004Impl_GetChannelValue,
    .SetIntegrationTime   = LTDriverLightSensorOPT3004Impl_SetIntegrationTime,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverLightSensorOPT3004, (ILTDriverLightSensor));

/********************************************************************************************************************************
 * Library startup and shutdown.
 */

static bool ConfigureDeviceUnit(LTDeviceConfig *pDeviceConfig, OPT3004_Instance *instance, u32 deviceUnitSection) {
    const char *controlBusName;
    if (!(controlBusName = pDeviceConfig->ReadString(deviceUnitSection, "i2c-bus"))) {
        LTLOG_YELLOWALERT("cdu.i2c.name", NULL);
        return false;
    }
    instance->cfg.i2cBusIndex = s_libI2C->GetBusIndexFromName(controlBusName);
    if (instance->cfg.i2cBusIndex == LT_U32_MAX) {
        LTLOG_YELLOWALERT("cdu.i2c.idx", NULL);
        return false;
    }
    if (!(instance->cfg.i2cAddress = pDeviceConfig->ReadInteger(deviceUnitSection, "i2c-address"))) {
        LTLOG_YELLOWALERT("cdu.i2c.addr", NULL);
        return false;
    }

    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */
    bool ret = false;
    LTDeviceConfig *pDeviceConfig = lt_openlibrary(LTDeviceConfig);
    u32 driverSection = pDeviceConfig->GetDriverSection("LTDeviceLightSensor", "LTDriverLightSensorOPT3004");
    s_DeviceUnits.nNumDeviceUnits = pDeviceConfig->GetNumDeviceUnits(driverSection);
    if (s_DeviceUnits.nNumDeviceUnits == 0) return true;

    if (!(s_DeviceUnits.pDeviceUnits = lt_malloc(s_DeviceUnits.nNumDeviceUnits * sizeof(OPT3004_Instance)))) {
        LTLOG_YELLOWALERT("cdus.oom", NULL);
        goto err;
    }
    lt_memset(s_DeviceUnits.pDeviceUnits, 0, s_DeviceUnits.nNumDeviceUnits * sizeof(OPT3004_Instance));

    for (u32 i = 0; i < s_DeviceUnits.nNumDeviceUnits; ++i) {
        OPT3004_Instance *instance = &s_DeviceUnits.pDeviceUnits[i];
        u32 deviceUnitSection = pDeviceConfig->GetDeviceUnitSectionAt(driverSection, i);
        if (!deviceUnitSection) {
            LTLOG_YELLOWALERT("cdus.no", NULL);
            goto err;
        }

        if (!ConfigureDeviceUnit(pDeviceConfig, instance, deviceUnitSection)) {
            LTLOG_YELLOWALERT("cdus.err", "Failed to configure device unit %lu", LT_Pu32(i));
            goto err;
        }
    }
    DLOG("cdus.ok", "Configured %lu device units", LT_Pu32(s_DeviceUnits.nNumDeviceUnits));
    ret = true;
err:
    lt_closelibrary(pDeviceConfig);
    return ret;
}

static void LTDriverLightSensorOPT3004Impl_LibFini(void) {
    if (s_DeviceUnits.pDeviceUnits) {
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i) {
            OPT3004_Instance *instance = &s_DeviceUnits.pDeviceUnits[i - 1];
            ShutdownSensor(instance);
        }
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;

    if (s_libI2C) {
        lt_closelibrary(s_libI2C);
        s_libI2C = NULL;
    }
}

static bool LTDriverLightSensorOPT3004Impl_LibInit(void) {
    do {
        if (!(s_libI2C = lt_openlibrary(LTDeviceI2C))) {
            LTLOG_YELLOWALERT("init.lib.i2c", "Error opening lib LTDeviceI2C");
            break;
        }
        if (!ConfigureDeviceUnits()) {
            LTLOG_YELLOWALERT("init.devs", "Error configuring device units");
            break;
        }

        // LTDeviceLightSensor does not handle device units, so initialise the first one for use.
        if (!s_DeviceUnits.pDeviceUnits) {
            LTLOG_YELLOWALERT("init.empty", "No sensors configured");
            break;
        }
        OPT3004_Instance *instance = &s_DeviceUnits.pDeviceUnits[0];
        if (!InitSensor(instance)) {
            LTLOG_YELLOWALERT("init.sensor", "Error initializing sensor");
            break;
        }

        return true;
    } while (0);

    LTDriverLightSensorOPT3004Impl_LibFini();
    return false;
}

