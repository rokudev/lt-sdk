/******************************************************************************
 * lt/source/lt/driver/imagesensor/sc223a/LTDriverImageSensorSC223A.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for SC223A image sensor
 *
 *****************************************************************************/
/** @file LTDriverImageSensorSC223A.c Implementation of SC223A image sensor driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/mipicsi/LTDeviceMipiCsi.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("sc223a");
#define P(...) LTLOG_DEBUG(__VA_ARGS__)

enum {
    kI2cBusFrequency        = 400000,
    kSensorId               = 0x3ecb,
    kISPSensorID            = 0xf223, // report 0xF223 on Get SensorAttribute_Id
    kISPSensorOutputWidth   = 1920,
    kISPSensorOutputHeight  = 1080,
    kISPSensorValidOffsetX  = 0,
    kISPSensorValidOffsetY  = 0,
};

typedef enum {
    SC223A_Register_SensorId = 0x3107,
} SC223A_Register;

typedef struct {
    u16 addr;
    u16 value;
} SensorSetting;

enum {
    kSensorSettingMax = 200,  // 0x2fc / 4
};
static SensorSetting s_SensorSettings[kSensorSettingMax];
static int s_SensorSettingCount;

static LTDeviceI2C     *s_libI2C;
static LTDeviceMipiCsi *s_libMipi;
static LTDevicePins    *s_libPins;
static ILTThread       *s_thread;
static ILTEvent        *s_event;

typedef enum {
    SC223A_State_PowerUpDelay,
    SC223A_State_ReadSensorId,
    SC223A_State_ApplySettings,
    SC223A_State_Idle,
    SC223A_State_Standby,
} SC223A_State;

// refer to struct ak_sensor_aec_parms
struct AecParams {
    int curr_again_level;
    int curr_again_10x;
    int r0x3e02_value;
    int r0x3e01_value;
    int r0x3e00_value;
    int curr_2x_dgain;
    int curr_corse_gain;
    int target_exp_ctrl;
    int r0x3e08_corse_gain;
    int r0x3e09_fine_gain;
    int reg_frame_hts;
    int reg_frame_vts;
    int pclk_freq;
    int calc_vts_tmp;
    int version;
};

// refer to struct sensor_fps_info
struct FpsInfo {
    int current_fps;
    int to_fps;
    int to_fps_value;
    int reg_fps_value;
    int rb_rows;
};

// refer to struct ae_fast_struct
struct AeFast {
    s32 sensor_exp_time;
    s32 sensor_a_gain;
    s32 sensor_d_gain;
    s32 isp_d_gain;
    struct {  // refer to AK_ISP_WB_GAIN, struct ak_isp_wb_gain
        u16 r_gain;
        u16 g_gain;
        u16 b_gain;
        s16 r_offset;
        s16 g_offset;
        s16 b_offset;
    } wb;
};

static struct AeFast s_aeFastDefault = {
    .sensor_exp_time = 2563,  // INIT_EXP_TIME       (2571 - EXP_DECREASE_LINES)
    .sensor_a_gain = 256,
    .sensor_d_gain = 256,
    .isp_d_gain = 256,
    {
        .r_gain = 445,
        .g_gain = 256,
        .b_gain = 396,
        .r_offset = 0,
        .g_offset = 0,
        .b_offset = 0
    }
};

// refer to isp_def_ae_tbl
#define MAX_AE_TABLE_CNT 256
// refer to AK_ISP_QUICK_AE_INFO, same struct as AK_ISP_AE_INFO, and re-define as in lt_ak_isp_drv.h
struct AeInfo {
    s32 a_gain;      // analog gain
    s32 d_gain;      // digital gain
    s32 isp_d_gain;  // isp digital gain
    s32 exp_time;    // exposure time
};
static struct AeInfo s_ispDefaultAeTable[] = {
    //again, dgain, isp_dgain, exp_time lumi region
    // group 0
    {1707,0, 256,1356}, //{0,  4}, night mode
    {1012,0, 256,1356}, //{5,  9},
    {536, 0, 256,1356}, //{10, 14},
    {438, 0, 256,1356}, //{15, 19},
    {268, 0, 256,1356}, //{20, 24},
    {536, 0, 256,1614}, //{25, 29},
    {438, 0, 256,1614}, //{30, 34},
    {268, 0, 256,1514}, //{35, 39},
    {365, 0, 256,678},  //{40, 44},
    {350, 0, 256,678},  //{45, 49},
    // group 10
    {456, 0, 256,339},  //{50, 59},
    {426, 0, 256,339},  //{60, 69},
    {416, 0, 256,339},  //{70, 79},
    {386, 0, 256,339},  //{80, 89},
    {356, 0, 256,339},  //{90, 99},
    {326, 0, 256,339},  //{95, 99},
    {256, 0, 256,339},  //{100,109},
    {256, 0, 256,300},  //{110,119},
    {256, 0, 256,277},  //{120,129},
    {256, 0, 256,231},  //{130,139},
    {256, 0, 256,211},  //{140,149},
    // group 20
    {256, 0, 256,200},  //{150,169},
    {256, 0, 256,100},  //{170,189},
    {256, 0, 256,30},   //{190,209},
    {260, 0, 256,10},   //{210,229},
    {256, 0, 256,5},    //{230,249},
    {256, 0, 256,1},    //{250,255},
};

typedef struct {
    LTAtomic         refCount;
    struct {
        const char  *name;
        u32          resetPinNumber;
        u32          powerdownPinNumber;
        u32          dataBusIndex;
        u32          controlBusIndex;
        u16          i2cAddress;
    } cfg;
    SC223A_State     state;
    LTEvent          hEvent;
    LTDeviceUnit     hResetPin;
    LTDeviceUnit     hPowerdownPin;
    LTDeviceUnit     hI2cBus;
    LTDeviceUnit     hMipiBus;
    LTOThread       *thread;
    LTOThread       *i2cThread;
    u16              sensorId;
    u32              nextSettingIndex;
    // frame rate and exposure control
    struct AecParams aecParams;
    struct FpsInfo   fpsInfo;
    struct AeFast    aeFast;
} SC223A_Instance;

/* Container for all the LED group instances */
typedef struct DeviceUnits {
    SC223A_Instance *pDeviceUnits;     /* Pointer into the heap where the Device Units are stored (as an array) */
    u32              nNumDeviceUnits;  /* How many Device Units this Driver supplies                            */
} DeviceUnits;

static DeviceUnits s_DeviceUnits;
static const LTArgsDescriptor s_SensorEventArgs = {
    .nNumArgs = 1, .argTypes = { kLTArgType_u32 }
};

static void InitSensorParams(SC223A_Instance *instance);

/********************************************************************************************************************************
 * Platform-specific image sensor initialization                                                                            */

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceImageSensor, LTDriverImageSensorSC223A);

static void DispatchSensorEvent(LTEvent hEvent, void *eventProc, LTArgs *eventArgs, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LTDeviceImageSensor_OnEventProc *callback = eventProc;
    LTImageSensorEvent event = LTArgs_u32At(0, eventArgs);
    callback(event, eventProcClientData);
}

static void
ReadRegister(SC223A_Instance *instance, u16 registerAddress, void *recvBuf, u32 recvBufSize) {
    u8 cmd[2] = {
        (registerAddress >> 8) & 0xFF,
        (registerAddress >> 0) & 0xFF,
    };
    instance->i2cThread = LT_GetCore()->GetCurrentThreadObject();
    s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress,
        recvBuf, recvBufSize,
        cmd, sizeof(cmd),
        true, true,
        NULL, instance);
}

static void
WriteRegister(SC223A_Instance *instance, u16 registerAddress, u8 value) {
    u8 cmd[3] = {
        (registerAddress >> 8) & 0xFF,
        (registerAddress >> 0) & 0xFF,
        value,
    };
    instance->i2cThread = LT_GetCore()->GetCurrentThreadObject();
    s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress,
        NULL, 0,
        cmd, sizeof(cmd),
        true, true,
        NULL, instance);
}

static LTDeviceUnit
InitPin(u32 pinUnitNumber) {
    LTDeviceUnit hPin = s_libPins->CreateDeviceUnitHandle(pinUnitNumber);
    if (!hPin) return 0;
    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin);
    if (!iPinBank) {
        lt_destroyhandle(hPin);
        return 0;
    }
    iPinBank->ConfigureAsOutput(hPin, kLTDevicePin_PinConfiguration_OutputType_OpenDrain);
    iPinBank->Set(hPin, 0);
    return hPin;
}

static void
PowerOnTimerProc(void *clientData) {
    s_thread->KillTimer(s_thread->GetCurrentThread(), PowerOnTimerProc, clientData);
    SC223A_Instance *instance = clientData;
    if (!s_libI2C->ProbeAddress(instance->hI2cBus, instance->cfg.i2cAddress)) {
        LTLOG_REDALERT("ins.probe", "I2C probe failed: addr=%0lx", LT_Pu32(instance->cfg.i2cAddress));
        s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOnFailed);
    }

    // get sensor id
    instance->state = SC223A_State_ReadSensorId;
    ReadRegister(instance, SC223A_Register_SensorId, &instance->sensorId, sizeof(instance->sensorId));
    if (instance->sensorId != kSensorId) {
        LTLOG_YELLOWALERT("bad.sensor.id", "invalid 0x%04lx expect 0x%04lx", LT_Pu32(instance->sensorId), LT_Pu32(kSensorId));
        s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOnFailed);
        return;
    }

    // apply settings
    instance->state = SC223A_State_ApplySettings;
    InitSensorParams(instance);

    // enable mipi
    instance->state = SC223A_State_Idle;
    s_libMipi->EnableOutput(instance->hMipiBus, true);
    P("st.setting", "done");
    s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOn);
}

static bool
InitSensor(SC223A_Instance *instance) {
    instance->thread = LT_GetCore()->GetCurrentThreadObject();
    if (!(instance->hEvent = LT_GetCore()->CreateEvent(&s_SensorEventArgs, DispatchSensorEvent, NULL, NULL, NULL))) {
        LTLOG_REDALERT("ins.crev", "Failed to create event");
        return false;
    }
    if (!(instance->hResetPin = InitPin(instance->cfg.resetPinNumber))) {
        LTLOG_REDALERT("ins.rst.open", "Failed to open reset pin");
        return false;
    }
    if (!(instance->hPowerdownPin = InitPin(instance->cfg.powerdownPinNumber))) {
        LTLOG_REDALERT("ins.pwdn.open", "Failed to open powerdown pin");
        return false;
    }
    if (!(instance->hMipiBus = s_libMipi->CreateDeviceUnitHandle(instance->cfg.dataBusIndex))) {
        LTLOG_REDALERT("ins.dbus.open", "Failed to open MIPI-CSI bus %lu", LT_Pu32(instance->cfg.dataBusIndex));
        return false;
    }
    if (!(instance->hI2cBus = s_libI2C->CreateDeviceUnitHandle(instance->cfg.controlBusIndex))) {
        LTLOG_REDALERT("ins.cbus.open", "Failed to open I2C bus %lu", LT_Pu32(instance->cfg.controlBusIndex));
        return false;
    }
    if (!(s_libI2C->SetDeviceConfiguration(instance->hI2cBus, &(LTDeviceI2C_Configuration) {.Async = false, .Master = true, .Frequency = kI2cBusFrequency }))) {
        LTLOG_REDALERT("ins.cbus.cfg", "Failed to configure I2C bus");
        return false;
    }
    return true;
}

static void
ShutdownSensor(SC223A_Instance *instance) {
    lt_destroyhandle(instance->hResetPin);
    lt_destroyhandle(instance->hPowerdownPin);
    lt_destroyhandle(instance->hI2cBus);
    lt_destroyhandle(instance->hMipiBus);
    lt_destroyhandle(instance->hEvent);
}

static u32
ConfigurePin(LTDeviceConfig *pDeviceConfig, u32 deviceUnitSection, const char *pinKeyName) {
    const char *pinName;
    if (!(pinName = pDeviceConfig->ReadString(deviceUnitSection, pinKeyName))) {
        LTLOG_REDALERT("cpn.nex", NULL);
        return LT_U32_MAX;
    }
    u32 pinUnitNumber;
    if (!s_libPins->GetUnitNumberFromBankName(pinName, &pinUnitNumber)) {
        LTLOG_REDALERT("cpn.gnum", NULL);
        return LT_U32_MAX;
    }
    return pinUnitNumber;
}

static bool
ConfigureDeviceUnit(LTDeviceConfig *pDeviceConfig, SC223A_Instance *instance, u32 deviceUnitSection) {
    if (!(instance->cfg.name = pDeviceConfig->ReadString(deviceUnitSection, "name"))) {
        LTLOG_REDALERT("cdu.name", NULL);
        return false;
    }
    const char *dataBusName;
    if (!(dataBusName = pDeviceConfig->ReadString(deviceUnitSection, "mipi-bus"))) {
        LTLOG_REDALERT("cdu.dbus.name", NULL);
        return false;
    }
    instance->cfg.dataBusIndex = s_libMipi->GetBusIndexFromName(dataBusName);
    if (instance->cfg.dataBusIndex == LT_U32_MAX) {
        LTLOG_REDALERT("cdu.dbus.idx", NULL);
        return false;
    }
    const char *controlBusName;
    if (!(controlBusName = pDeviceConfig->ReadString(deviceUnitSection, "i2c-bus"))) {
        LTLOG_REDALERT("cdu.cbus.name", NULL);
        return false;
    }
    instance->cfg.controlBusIndex = s_libI2C->GetBusIndexFromName(controlBusName);
    if (instance->cfg.controlBusIndex == LT_U32_MAX) {
        LTLOG_REDALERT("cdu.cbus.idx", NULL);
        return false;
    }
    if (!(instance->cfg.i2cAddress = pDeviceConfig->ReadInteger(deviceUnitSection, "i2c-address"))) {
        LTLOG_REDALERT("cdu.addr", NULL);
        return false;
    }
    instance->cfg.resetPinNumber = ConfigurePin(pDeviceConfig, deviceUnitSection, "reset-pin");
    if (instance->cfg.resetPinNumber == LT_U32_MAX) {
        LTLOG_REDALERT("cdu.rst", NULL);
        return false;
    }
    instance->cfg.powerdownPinNumber = ConfigurePin(pDeviceConfig, deviceUnitSection, "powerdown-pin");
    if (instance->cfg.powerdownPinNumber == LT_U32_MAX) {
        LTLOG_REDALERT("cdu.pwdn", NULL);
        return false;
    }
    return true;
}

static bool
ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;   /* already configured - do not allocate and configure again */
    LTDeviceConfig *pDeviceConfig = lt_openlibrary(LTDeviceConfig);
    u32 driverSection = pDeviceConfig->GetDriverSection("LTDeviceImageSensor", "LTDriverImageSensorSC223A");
    s_DeviceUnits.nNumDeviceUnits = pDeviceConfig->GetNumDeviceUnits(driverSection);
    if (s_DeviceUnits.nNumDeviceUnits == 0) return true;

    if (!(s_DeviceUnits.pDeviceUnits = lt_malloc(s_DeviceUnits.nNumDeviceUnits * sizeof(SC223A_Instance)))) {
        LTLOG_REDALERT("cdus.oom", NULL);
        return false;
    }
    lt_memset(s_DeviceUnits.pDeviceUnits, 0, s_DeviceUnits.nNumDeviceUnits * sizeof(SC223A_Instance));

    for (u32 i = 0; i < s_DeviceUnits.nNumDeviceUnits; ++i) {
        SC223A_Instance *instance = &s_DeviceUnits.pDeviceUnits[i];
        u32 deviceUnitSection = pDeviceConfig->GetDeviceUnitSectionAt(driverSection, i);
        if (!deviceUnitSection) {
            LTLOG_YELLOWALERT("cdus.no", NULL);
            goto err;
        }

        if (!ConfigureDeviceUnit(pDeviceConfig, instance, deviceUnitSection)) {
            LTLOG_REDALERT("cdus.err", "Failed to configure device unit %lu", LT_Pu32(i));
            goto err;
        }
    }
    return true;

err:
    return false;
}

/*******************************************************************************
 * Cleanup or bailure
 * Tear down all Device Units and reclaim resources.                          */

static void
ShutdownDeviceUnit(SC223A_Instance *instance) {
    LT_UNUSED(instance);
}

static void
Shutdown(void) {
    if (s_DeviceUnits.pDeviceUnits) {
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i) {
            SC223A_Instance *instance = &s_DeviceUnits.pDeviceUnits[i - 1];
            ShutdownDeviceUnit(instance);
        }
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;
    lt_closelibrary(s_libMipi); s_libMipi = NULL;
    lt_closelibrary(s_libI2C); s_libI2C = NULL;
    lt_closelibrary(s_libPins); s_libPins = NULL;
    s_thread = NULL;
}

/********************************************************************************************************************************
 * Library initialization and deinitialization                                                                                 */
static bool
LTDriverImageSensorSC223AImpl_LibInit(void) {
    if (!(s_thread = lt_getlibraryinterface(ILTThread, LT_GetCore())) ||
        !(s_event = lt_getlibraryinterface(ILTEvent, LT_GetCore())) ||
        !(s_libPins = lt_openlibrary(LTDevicePins)) ||
        !(s_libI2C = lt_openlibrary(LTDeviceI2C)) ||
        !(s_libMipi = lt_openlibrary(LTDeviceMipiCsi)) ||
        !ConfigureDeviceUnits()) {
        Shutdown();
        return false;
    }
    return true;
}

static void
LTDriverImageSensorSC223AImpl_LibFini(void) {
    Shutdown();
}

static ILTDriverImageSensor s_ILTDriverImageSensor;

/********************************************************************************************************************************
 * Device-unit creation interface.
 */
static u32 LTDriverImageSensorSC223AImpl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

static LTDeviceUnit
LTDriverImageSensorSC223AImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    if (nDeviceUnitNumber >= s_DeviceUnits.nNumDeviceUnits) return 0;
    LTDeviceUnit hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverImageSensor, sizeof(SC223A_Instance *));
    if (!hDevice) return 0;
    bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                   wrong with preparing the handle or initializing the interface. */
    SC223A_Instance **pinstance = (SC223A_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    if (pinstance) {
        SC223A_Instance *instance = *pinstance = &s_DeviceUnits.pDeviceUnits[nDeviceUnitNumber];
        bInterfaceOK = true;    /* all okay, unless first-reference initialization (below) fails */
        if (LTAtomic_FetchAdd(&instance->refCount, 1) == 0) {   /* Just starting up this instance. */
            bInterfaceOK = InitSensor(instance);
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, pinstance);
    }
    if (!bInterfaceOK) {
        lt_destroyhandle(hDevice);
        hDevice = 0;
    }
    return hDevice;
}

static SC223A_Instance *InstanceFromHandle(LTDeviceUnit hDevice) {
    SC223A_Instance **pinstance = (SC223A_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    SC223A_Instance *instance = NULL;
    if (pinstance) {
        instance = *pinstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, pinstance);
    }
    return instance;
}

static void
OnDestroyHandle(LTHandle hDevice) {
    SC223A_Instance *instance = InstanceFromHandle(hDevice);
    if (instance && LTAtomic_FetchSubtract(&instance->refCount, 1) == 1) {
        ShutdownSensor(instance);
    }
}

static void LTDriverImageSensorSC223AImpl_OnImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc, void *clientData) {
    SC223A_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;
    s_event->RegisterForEvent(instance->hEvent, eventProc, NULL, clientData, false);
}

static void LTDriverImageSensorSC223AImpl_NoImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc) {
    SC223A_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;
    s_event->UnregisterFromEvent(instance->hEvent, eventProc);
}

static void
LTDriverImageSensorSC223AImpl_PowerOn(LTDeviceUnit hUnit) {
    SC223A_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;

    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    iPinBank->Set(instance->hResetPin, 1);
    iPinBank->Set(instance->hPowerdownPin, 1);

    // Enable EXTCLK output to sensor and wait 4ms before talking over I2C per the datasheet
    s_libMipi->EnableExtclk(instance->hMipiBus, true);
    instance->state = SC223A_State_PowerUpDelay;
    s_thread->SetTimer(s_thread->GetCurrentThread(), LTTime_Milliseconds(4), PowerOnTimerProc, NULL, instance);
}

static void
LTDriverImageSensorSC223AImpl_PowerOff(LTDeviceUnit hUnit) {
    SC223A_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;

    s_libMipi->EnableExtclk(instance->hMipiBus, false);

    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    iPinBank->Set(instance->hResetPin, 0);
    iPinBank->Set(instance->hPowerdownPin, 0);
}

static u8 ReadRegOneByte(SC223A_Instance *instance, u16 registerAddress) {
    u8 val = 0;
    ReadRegister(instance, registerAddress, &val, sizeof(u8));
    return val;
}

// refer to ak_sensor_update_a_gain_func
static void UpdateAGain(SC223A_Instance *instance, const int a_gain) {
    // max again < 25 times = 8192
    int const gain = a_gain * 1000 / 256;
    int ana_gain = 0x00;
    int dig_gain = 0x00;
    int dig_fine_gain = 0x80;
    int range = 0;
    //x = gain / 1000;
    range = 0xff - 0x80;

    if (gain < 1000) {
        ana_gain = 0x00;
        dig_gain = 0x00;
        dig_fine_gain = 0x80;
    } else if (gain < 1810) {
        ana_gain = 0x00;
        dig_gain = 0x00;
        dig_fine_gain = (gain - 1000) * range / (1810 - 1000) + 0x80;
    } else if (gain < 3620) {
        ana_gain = 0x40;
        dig_gain = 0x00;
        dig_fine_gain = (gain - 1810) * range / (3620 - 1810) + 0x80;
    } else if (gain < 7240) {
        ana_gain = 0x48;
        dig_gain = 0x00;
        dig_fine_gain = (gain - 3620) * range / (7240 - 3620) + 0x80;
    } else if (gain < 14480) {
        ana_gain = 0x49;
        dig_gain = 0x00;
        dig_fine_gain = (gain - 7240) * range / (14480 - 7240) + 0x80;
    } else if (gain < 28960) {
        ana_gain = 0x4b;
        dig_gain = 0x00;
        dig_fine_gain = (gain - 14480) * range / (28960 - 14480) + 0x80;
    } else if (gain <= 57920)  {
        ana_gain = 0x4f;
        dig_gain = 0x00;
        dig_fine_gain = (gain - 28960) * range / (57920 - 28960) + 0x80;
    } else {
        ana_gain = 0x5f;
        dig_gain = 0x00;
        dig_fine_gain = 0x80;
    }

    WriteRegister(instance, 0x3e09, ana_gain);       // AGAIN_REG   (0x3e09)
    dig_fine_gain = dig_fine_gain & 0xfc;
    WriteRegister(instance, 0x3e06, dig_gain);       // DGAIN_REG   (0x3e06)
    WriteRegister(instance, 0x3e07, dig_fine_gain);  // D_FINE_GAIN_REG    (0x3e07)
}

// refer to ak_sensor_get_vts
static u32 GetVts(SC223A_Instance *instance) {
    u32 vts = ReadRegOneByte(instance, 0x320e);
    vts <<= 8;
    vts |= ReadRegOneByte(instance, 0x320f);
    return vts;
}

// refer to ak_sensor_update_vts
static void UpdateVts(SC223A_Instance *instance, u32 vts) {
    WriteRegister(instance, 0x320e, vts >> 8);    // VTS_REG_H   (0x320e)
    WriteRegister(instance, 0x320f, vts & 0xff);  // VTS_REG_L   (0x320f)
    instance->fpsInfo.reg_fps_value = vts;
}

// refer to ak_sensor_update_exp
static void UpdateExp(SC223A_Instance *instance, u32 exp) {
    if (exp < 1) exp = 1;
    if (exp > 8191) exp = 8191; // 2^13
    instance->aecParams.r0x3e00_value = (exp >> 12) & 0x0f;
    instance->aecParams.r0x3e01_value = (exp >> 4) & 0xff;
    instance->aecParams.r0x3e02_value = ((exp)&0xf) << 4;
    WriteRegister(instance, 0x3e02, instance->aecParams.r0x3e02_value);  // EXP_REG_L   (0x3e02)
    WriteRegister(instance, 0x3e01, instance->aecParams.r0x3e01_value);  // EXP_REG_H   (0x3e01)
    WriteRegister(instance, 0x3e00, instance->aecParams.r0x3e00_value);  // EXP_REG_E   (0x3e00)
}

// refer to update_frame_vts_and_exp_ctrl
static void UpdateFrameVtsAndExpCtrl(SC223A_Instance *instance) {
    int const cur_frame_vts = GetVts(instance);
    int const target_frame_vts = instance->fpsInfo.to_fps_value;

    int max_exp_ctrl = instance->fpsInfo.to_fps_value * 2 - 8 - 1; // vts * 2 - 8, EXP_DECREASE_LINES = 8

    int target_exp_ctrl = 0;
    if (instance->aecParams.target_exp_ctrl <= max_exp_ctrl) target_exp_ctrl = instance->aecParams.target_exp_ctrl;
    else target_exp_ctrl = max_exp_ctrl;

    if ((target_frame_vts <= 0) || (target_exp_ctrl <= 0)) return;

    if (target_frame_vts >= cur_frame_vts) {
        if (target_frame_vts != cur_frame_vts) UpdateVts(instance, target_frame_vts);
        UpdateExp(instance, target_exp_ctrl);
    } else {
        UpdateExp(instance, target_exp_ctrl);
        UpdateVts(instance, target_frame_vts);
    }
}

// refer to ak_sensor_update_exp_time_func
static int UpdateExpTimeFunc(SC223A_Instance *instance, u32 expTime) {
    instance->aecParams.target_exp_ctrl = expTime;
    UpdateFrameVtsAndExpCtrl(instance);
    return 1; // EXP_EFFECT_FRAMES;
}

// refer to ak_sensor_fps_to_vts
static int FpsToVts(SC223A_Instance *instance, const int fps) {
    int vts;
    switch (fps) {
        case 12:
            vts = 1250;  //50Hz ripple
            break;

        case 14:
            vts = 1428;  //50Hz ripple
            break;

        default:
            vts = fps * 100;
            break;
    }
    return instance->aecParams.calc_vts_tmp / vts;
}

// refer to sc223a_set_init_exp_fps
static void SetInitExpFps(SC223A_Instance *instance, int exp) {
    int vts = exp + 8;  // EXP_DECREASE_LINES (8)
    int fps = instance->aecParams.calc_vts_tmp / (vts * 100);
    if (fps > 30) {
        fps = 30;
        vts = instance->aecParams.calc_vts_tmp / (fps * 100);
    } else if (fps < 10) {
        fps = 10;
        vts = instance->aecParams.calc_vts_tmp / (fps * 100);
    }
    instance->fpsInfo.to_fps_value = FpsToVts(instance, fps);
    instance->fpsInfo.to_fps = fps;
    instance->fpsInfo.current_fps = fps;
    instance->aecParams.target_exp_ctrl = exp;
    UpdateFrameVtsAndExpCtrl(instance);
}

// refer to ak_sensor_agc_mode
static void AgcMode(SC223A_Instance *instance) {
    int value = ReadRegOneByte(instance, 0x3e03) & 0xf0;
    value |= 0xb;
    WriteRegister(instance, 0x3e03, value);
}

// refer to aec_parms_init
static void InitAecParams(struct AecParams *aecParams) {
    aecParams->curr_again_level = 0;
    aecParams->curr_again_10x = -1;
    aecParams->r0x3e02_value = 0;
    aecParams->r0x3e01_value = 0;
    aecParams->r0x3e00_value = 0;
    aecParams->curr_2x_dgain = 0;
    aecParams->curr_corse_gain = -1;
    aecParams->r0x3e08_corse_gain = 0;
    aecParams->r0x3e09_fine_gain = 0;
}

// refer to ak_sensor_init_func
static void InitSensorParams(SC223A_Instance *instance) {
    // init aec params
    InitAecParams(&instance->aecParams);
    instance->aeFast = s_aeFastDefault;

    // refer to ak_sensor_parms_init_f
    int vts_h_tmp = -1;
    int vts_l_tmp = -1;
    for (int i = 0; i < s_SensorSettingCount; ++i) {
        if (s_SensorSettings[i].addr == 0x320e) vts_h_tmp = s_SensorSettings[i].value;        // VTS_REG_H   (0x320e)
        else if (s_SensorSettings[i].addr == 0x320f) vts_l_tmp = s_SensorSettings[i].value;   // VTS_REG_L   (0x320f)
    }
    if (vts_h_tmp < 0) vts_h_tmp = 0x4;
    if (vts_l_tmp < 0) vts_l_tmp = 0x65;
    instance->aecParams.calc_vts_tmp = (vts_h_tmp << 8 | vts_l_tmp) * 30 * 100;  // SENSOR_MAX_FPS = 30
    instance->fpsInfo.rb_rows = 4;

    for (int i = 0; i < s_SensorSettingCount; ++i) {
        if (s_SensorSettings[i].addr == 0x3e01) {
            SetInitExpFps(instance, instance->aeFast.sensor_exp_time);
            UpdateAGain(instance, instance->aeFast.sensor_a_gain);
            // TODO: ak_sensor_set_flip_mirror(instance, instance->sensor_param.flip_mirror, 1);
        }
        WriteRegister(instance, s_SensorSettings[i].addr, s_SensorSettings[i].value);
    }

    AgcMode(instance);
}

// refer to ak_sensor_set_fps_func
static void SetFps(SC223A_Instance *instance, int fps) {
    instance->fpsInfo.to_fps_value = FpsToVts(instance, fps);
    instance->fpsInfo.to_fps = fps;
    UpdateFrameVtsAndExpCtrl(instance);
    instance->fpsInfo.current_fps = instance->fpsInfo.to_fps;
}

static void GetFlipMirror(SC223A_Instance *instance, u8 *flags) {
    int value = ReadRegOneByte(instance, 0x3221);   // FLIP_MIRROR    (0x3221)
    flags[0] = ((value & (0x3 << 5)) != 0);
    flags[1] = ((value & (0x3 << 1)) != 0);
}

// refer to ak_sensor_set_flip_mirror
static void SetFlipMirror(SC223A_Instance *instance, bool bFlipV, bool bFlipH) {
    int value = 0;
    if (bFlipV) value |= 0x3 << 5;
    else value &= ~(0x3 << 5);
    if (bFlipH) value |= 0x3 << 1;
    else value &= ~(0x3 << 1);
    WriteRegister(instance, 0x3221, value);   // FLIP_MIRROR    (0x3221)
}

static bool LTDriverImageSensorSC223AImpl_SetAttribute(LTDeviceUnit hUnit, LTImageSensorAttribute attr, const void *attrValue) {
    SC223A_Instance *instance = InstanceFromHandle(hUnit);
    switch (attr) {
        case kLTImageSensorAttribute_TuningData:
        {
            const LTImageSensorAttributeParams *v = attrValue;
            if (v->inLen > sizeof(s_SensorSettings) || v->inLen % sizeof(SensorSetting) != 0) {
                LTLOG_YELLOWALERT("tuning.len.err", " ");
                return false;
            }
            lt_memcpy(s_SensorSettings, v->in, v->inLen);
            s_SensorSettingCount = v->inLen / sizeof(SensorSetting);
            break;
        }

        case kLTImageSensorAttribute_Exposure:
        {
            int e = *(int *)attrValue;
            UpdateExpTimeFunc(instance, (u32)e);
            break;
        }

        case kLTImageSensorAttribute_FrameRate:
        {   // refer to ak_sensor_set_fps_direct
            int fps = *(int *)attrValue;
            if (instance->fpsInfo.current_fps != fps) SetFps(instance, fps);
            break;
        }

        case kLTImageSensorAttribute_AGain:
        {
            int aGain = *(int *)attrValue;
            UpdateAGain(instance, aGain);
            break;
        }

        case kLTImageSensorAttribute_FlipVerticalHorizontal:
        {
            const LTImageSensorAttributeParams *v = attrValue;
            if (v->inLen != 2) return false;
            const u8 *flips = v->in;
            SetFlipMirror(instance, flips[0], flips[1]);
            break;
        }

        default:
            return false;
    }
    return true;
}

// refer to _get_def_ae_table
static void GetDefaultAeTable(struct AeInfo *ae_tbl_data) {
    if (!ae_tbl_data) return;

    int i = 0;
    int group_id = 0;
    for (i = 0; i < 50; i++) {
        ae_tbl_data[i].d_gain = 0;
        ae_tbl_data[i].a_gain = s_ispDefaultAeTable[group_id + i / 5].a_gain;
        ae_tbl_data[i].exp_time = s_ispDefaultAeTable[group_id + i / 5].exp_time;
        ae_tbl_data[i].isp_d_gain = s_ispDefaultAeTable[group_id + i / 5].isp_d_gain;
    }

    group_id = 10;
    for (i = 50; i < 150; ++i) {
        ae_tbl_data[i].d_gain = 0;
        ae_tbl_data[i].a_gain = s_ispDefaultAeTable[group_id + (i - 50) / 10].a_gain;
        ae_tbl_data[i].exp_time = s_ispDefaultAeTable[group_id + (i - 50) / 10].exp_time;
        ae_tbl_data[i].isp_d_gain = s_ispDefaultAeTable[group_id + (i - 50) / 10].isp_d_gain;
    }

    group_id = 20;
    for (i = 150; i < MAX_AE_TABLE_CNT; ++i) {
        ae_tbl_data[i].d_gain = 0;
        ae_tbl_data[i].a_gain = s_ispDefaultAeTable[group_id + (i - 150) / 20].a_gain;
        ae_tbl_data[i].exp_time = s_ispDefaultAeTable[group_id + (i - 150) / 20].exp_time;
        ae_tbl_data[i].isp_d_gain = s_ispDefaultAeTable[group_id + (i - 150) / 20].isp_d_gain;
    }
}

static void GetSensorStatus(SC223A_Instance *instance) {
    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    u32 rPin = iPinBank->Read(instance->hResetPin);
    u32 pPin = iPinBank->Read(instance->hPowerdownPin);
    LTLOG_SERVER("pin.status", "reset %lu pow %lu", LT_Pu32(rPin), LT_Pu32(pPin));
}

static bool LTDriverImageSensorSC223AImpl_GetAttribute(LTDeviceUnit hUnit, LTImageSensorAttribute attr, void *attrValue) {
    SC223A_Instance *instance = InstanceFromHandle(hUnit);
    switch (attr) {
        case kLTImageSensorAttribute_Id:
            *(u32 *)attrValue = kISPSensorID;
            break;

        case kLTImageSensorAttribute_FrameRate:
            *(int *)attrValue = instance->fpsInfo.current_fps;
            break;

        case kLTImageSensorAttribute_ExposureForFps:
        {   // refer to ak_sensor_get_max_exp_for_fps
            LTImageSensorAttributeParams *param = attrValue;
            int fps = *(int *)param->in;
            int vts = FpsToVts(instance, fps);
            *(int *)param->out = vts *2 - 8;
            break;
        }

        case kLTImageSensorAttribute_Exposure:
            lt_memcpy(attrValue, &s_aeFastDefault, sizeof(s_aeFastDefault));
            break;
        
        case kLTImageSensorAttribute_AeTable:
            GetDefaultAeTable((struct AeInfo *)attrValue);
            break;

        case kLTImageSensorAttribute_FlipVerticalHorizontal:
        {
            LTImageSensorAttributeParams *v = attrValue;
            if (v->outLen != 2) return false;
            GetFlipMirror(instance, (u8 *)v->out);
            break;
        }

        case kLTImageSensorAttribute_Status:
            GetSensorStatus(instance);
            break;

        case kLTImageSensorAttribute_Crop:
        {
            LTImageSensorCropCap *a = (LTImageSensorCropCap *)attrValue;
            a->bounds.width = kISPSensorOutputWidth;
            a->bounds.height = kISPSensorOutputHeight;
            a->bounds.left = kISPSensorValidOffsetX;
            a->bounds.top = kISPSensorValidOffsetY;
            a->defrect = a->bounds;
            a->pixelaspect.numerator   = 1;
            a->pixelaspect.denominator = 1;
            break;
        }

        default:
            return false;
    }

    return true;
}

define_LTLIBRARY_INTERFACE(ILTDriverImageSensor,OnDestroyHandle) {
    .OnImageSensorEvent = LTDriverImageSensorSC223AImpl_OnImageSensorEvent,
    .NoImageSensorEvent = LTDriverImageSensorSC223AImpl_NoImageSensorEvent,
    .PowerOn            = LTDriverImageSensorSC223AImpl_PowerOn,
    .PowerOff           = LTDriverImageSensorSC223AImpl_PowerOff,
    .SetAttribute       = LTDriverImageSensorSC223AImpl_SetAttribute,
    .GetAttribute       = LTDriverImageSensorSC223AImpl_GetAttribute,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverImageSensorSC223A, (ILTDriverImageSensor))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Jan-24   trajan      created
 */
