/******************************************************************************
 * lt/source/lt/driver/imagesensor/jx-f51/LTDriverImageSensorJXF51.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for JX-F51 image sensor
 *
 *****************************************************************************/
/** @file LTDriverImageSensorJXF51.c Implementation of JX-F51 image sensor driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/mipicsi/LTDeviceMipiCsi.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("jxf51");
#define P(...) LTLOG_DEBUG(__VA_ARGS__)

enum {
    kI2cBusFrequency        = 100000,
    kI2CBusFreeTimeUs       = 10,             // Datasheet: 1.3uS minimum. give10us
    kSensorId               = 0x0f51,
    kSensorSettingMax       = 512,
    kSensorMaxFPS           = 30,
    kSensorExposureTimeLimit = 4,
    kISPSensorOutputWidth   = 1536,
    kISPSensorOutputHeight  = 1536,
    kISPSensorValidOffsetX  = 0,
    kISPSensorValidOffsetY  = 0,
};

// MIPI clock = 348.16MHz
// Pixel clk = MIPI clock / 10
// Pixel clk / FPS = frameTime
// W = frame width time
// H = frame height time
// frameTime = W x H
// W seems unimportant except to multiple H to get appropriate frameTime
// H decides exposure time limit
// Still good to ensure W value is defined and keep aspect ratio with H,
// in case W serves some purpose in frame timing.
typedef struct {
    u32 frameTimeH;
    u32 frameTimeW;
} JXF51_FrameTime;

// // Precalculated frame times for different FPS values with 348.16MHz MIPI clock
static JXF51_FrameTime s_FPSToFrameTime[] = {
    {0	 ,  0},         // 0 FPS
    {9974,	3491},
    {7052,	2468},
    {5758,	2015},
    {4987,	1745},
    {4460,	1561},
    {4072,	1425},
    {3770,	1319},
    {3526,	1234},
    {3325,	1164},
    {3154,	1104},
    {3007,	1053},
    {2879,	1008},
    {2766,	968},
    {2666,	933},
    {2575,	901},
    {2493,	873},
    {2419,	847},
    {2351,	823},
    {2288,	801},
    {2230,	781},       // 20 FPS
    {2176,	762},
    {2126,	744},
    {2080,	728},
    {2036,	713},
    {1995,	698},
    {1956,	685},
    {1919,	672},
    {1885,	660},
    {1852,	648},
    {1821,	637},       // 30 FPS
};

typedef enum {
    JXF51_Register_SensorId_H = 0x0A,
    JXF51_Register_SensorId_L = 0x0B,
} JXF51_Register;

typedef struct {
    u16 addr;
    u16 value;
} SensorSetting;

static SensorSetting s_SensorSettings[kSensorSettingMax];
static int s_SensorSettingCount;

static LTDeviceI2C     *s_libI2C;
static LTDeviceMipiCsi *s_libMipi;
static LTDevicePins    *s_libPins;
static ILTThread       *s_thread;
static ILTEvent        *s_event;

typedef enum {
    JXF51_State_PowerUpDelay,
    JXF51_State_ReadSensorId,
    JXF51_State_ApplySettings,
    JXF51_State_Idle,
    JXF51_State_Standby,
} JXF51_State;

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
    u32 current_fps;
    u32 to_fps;
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
    .sensor_exp_time = 2563,  // INIT_EXP_T¬IME       (2571 - EXP_DECREASE_LINES)
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
    JXF51_State     state;
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
    u8               r0x27_value;
} JXF51_Instance;

/* Container for all the LED group instances */
typedef struct DeviceUnits {
    JXF51_Instance *pDeviceUnits;     /* Pointer into the heap where the Device Units are stored (as an array) */
    u32              nNumDeviceUnits;  /* How many Device Units this Driver supplies                            */
} DeviceUnits;

static DeviceUnits s_DeviceUnits;
static const LTArgsDescriptor s_SensorEventArgs = {
    .nNumArgs = 1, .argTypes = { kLTArgType_u32 }
};

static void InitSensorParams(JXF51_Instance *instance);

/********************************************************************************************************************************
 * Platform-specific image sensor initialization                                                                            */

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceImageSensor, LTDriverImageSensorJXF51);

static void DispatchSensorEvent(LTEvent hEvent, void *eventProc, LTArgs *eventArgs, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LTDeviceImageSensor_OnEventProc *callback = eventProc;
    LTImageSensorEvent event = LTArgs_u32At(0, eventArgs);
    callback(event, eventProcClientData);
}

static void
ReadRegister(JXF51_Instance *instance, u8 registerAddress, void *recvBuf, u32 recvBufSize) {
    u8 cmd[1] = {
        (registerAddress >> 0) & 0xFF,
    };
    instance->i2cThread = LT_GetCore()->GetCurrentThreadObject();

    if (s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress,
        NULL, 0,
        cmd, sizeof(cmd),
        true, true,
        NULL, instance)) {
            P("ins.r.addr", "set read from 0x%02x success", registerAddress);
    } else {
        LTLOG("ins.r.addr.fail", "set read addr to 0x%02x failed", registerAddress);
        return;
    }
    s_thread->Sleep(LTTime_Microseconds(kI2CBusFreeTimeUs));

    if (s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress + 1,
        recvBuf, recvBufSize,
        NULL, 0,
        true, true,
        NULL, instance)) {
            P("ins.rd", "read 0x%02x success", registerAddress);
    } else {
        LTLOG("ins.rd.fail", "read from 0x%02x failed", registerAddress);
    }
    s_thread->Sleep(LTTime_Microseconds(kI2CBusFreeTimeUs));
}

static void
WriteRegister(JXF51_Instance *instance, u8 registerAddress, u8 value) {
    u8 cmd[2] = {
        (registerAddress >> 0) & 0xFF,
        value,
    };
    instance->i2cThread = LT_GetCore()->GetCurrentThreadObject();
    if (s_libI2C->I2CMasterTransfer(instance->hI2cBus, instance->cfg.i2cAddress,
        NULL, 0,
        cmd, sizeof(cmd),
        true, true,
        NULL, instance)) {
            P("ins.wr", "write 0x%02x to 0x%02x success", value, registerAddress);
    } else {
        LTLOG("ins.wr", "write 0x%02x to 0x%02x failed", value, registerAddress);
    }
    s_thread->Sleep(LTTime_Microseconds(kI2CBusFreeTimeUs));
}

static u8 ReadRegOneByte(JXF51_Instance *instance, u8 registerAddress) {
    u8 val = 0;
    ReadRegister(instance, registerAddress, &val, sizeof(u8));
    return val;
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
    JXF51_Instance *instance = clientData;

    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    iPinBank->Set(instance->hResetPin, 0);
    s_thread->Sleep(LTTime_Milliseconds(15));
    iPinBank->Set(instance->hResetPin, 1);
    s_thread->Sleep(LTTime_Milliseconds(1));
    iPinBank->Set(instance->hPowerdownPin, 0);
    s_thread->Sleep(LTTime_Milliseconds(30));


    if (!s_libI2C->ProbeAddress(instance->hI2cBus, instance->cfg.i2cAddress)) {
        LTLOG_REDALERT("ins.probe", "I2C probe failed: addr=%0lx", LT_Pu32(instance->cfg.i2cAddress));
        // Hack: Report success when probe fails
        s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOn);
        return;
        // TODO: When we have Taft hardware, we can report failure
        //s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOnFailed);
    }

    s_thread->Sleep(LTTime_Microseconds(kI2CBusFreeTimeUs));

    WriteRegister(instance, 0x12, 0x80); // reset sensor
    s_thread->Sleep(LTTime_Milliseconds(100));

    // get sensor id
    instance->state = JXF51_State_ReadSensorId;

    instance->sensorId = ReadRegOneByte(instance, JXF51_Register_SensorId_H);
    instance->sensorId  <<= 8;
    instance->sensorId |= ReadRegOneByte(instance, JXF51_Register_SensorId_L);

    if (instance->sensorId != kSensorId) {
        LTLOG_YELLOWALERT("bad.sensor.id", "invalid 0x%04lx expect 0x%04lx", LT_Pu32(instance->sensorId), LT_Pu32(kSensorId));
        s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOnFailed);
        return;
    }

    // apply settings
    instance->state = JXF51_State_ApplySettings;
    InitSensorParams(instance);

    // enable mipi

    //WriteRegister(instance, 0x6C, 0xA0); // DPHY2: MIPI power down -> normal mode
    
    instance->state = JXF51_State_Idle;
    s_libMipi->EnableOutput(instance->hMipiBus, true);

    s_event->NotifyEvent(instance->hEvent, kLTImageSensorEvent_PowerOn);
}

static bool
InitSensor(JXF51_Instance *instance) {
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
ShutdownSensor(JXF51_Instance *instance) {
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
ConfigureDeviceUnit(LTDeviceConfig *pDeviceConfig, JXF51_Instance *instance, u32 deviceUnitSection) {
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
    u32 driverSection = pDeviceConfig->GetDriverSection("LTDeviceImageSensor", "LTDriverImageSensorJXF51");
    s_DeviceUnits.nNumDeviceUnits = pDeviceConfig->GetNumDeviceUnits(driverSection);
    if (s_DeviceUnits.nNumDeviceUnits == 0) return true;

    if (!(s_DeviceUnits.pDeviceUnits = lt_malloc(s_DeviceUnits.nNumDeviceUnits * sizeof(JXF51_Instance)))) {
        LTLOG_REDALERT("cdus.oom", NULL);
        return false;
    }
    lt_memset(s_DeviceUnits.pDeviceUnits, 0, s_DeviceUnits.nNumDeviceUnits * sizeof(JXF51_Instance));

    for (u32 i = 0; i < s_DeviceUnits.nNumDeviceUnits; ++i) {
        JXF51_Instance *instance = &s_DeviceUnits.pDeviceUnits[i];
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
ShutdownDeviceUnit(JXF51_Instance *instance) {
    LT_UNUSED(instance);
}

static void
Shutdown(void) {
    if (s_DeviceUnits.pDeviceUnits) {
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i) {
            JXF51_Instance *instance = &s_DeviceUnits.pDeviceUnits[i - 1];
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
LTDriverImageSensorJXF51Impl_LibInit(void) {
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
LTDriverImageSensorJXF51Impl_LibFini(void) {
    Shutdown();
}

static ILTDriverImageSensor s_ILTDriverImageSensor;

/********************************************************************************************************************************
 * Device-unit creation interface.
 */
static u32 LTDriverImageSensorJXF51Impl_GetNumDeviceUnits(void) { return s_DeviceUnits.nNumDeviceUnits; }

static LTDeviceUnit
LTDriverImageSensorJXF51Impl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    if (nDeviceUnitNumber >= s_DeviceUnits.nNumDeviceUnits) return 0;
    LTDeviceUnit hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverImageSensor, sizeof(JXF51_Instance *));
    if (!hDevice) return 0;
    bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                   wrong with preparing the handle or initializing the interface. */
    JXF51_Instance **pinstance = (JXF51_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    if (pinstance) {
        JXF51_Instance *instance = *pinstance = &s_DeviceUnits.pDeviceUnits[nDeviceUnitNumber];
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

static JXF51_Instance *InstanceFromHandle(LTDeviceUnit hDevice) {
    JXF51_Instance **pinstance = (JXF51_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    JXF51_Instance *instance = NULL;
    if (pinstance) {
        instance = *pinstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, pinstance);
    }
    return instance;
}

static void
OnDestroyHandle(LTHandle hDevice) {
    JXF51_Instance *instance = InstanceFromHandle(hDevice);
    if (instance && LTAtomic_FetchSubtract(&instance->refCount, 1) == 1) {
        ShutdownSensor(instance);
    }
}

static void LTDriverImageSensorJXF51Impl_OnImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc, void *clientData) {
    JXF51_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;
    s_event->RegisterForEvent(instance->hEvent, eventProc, NULL, clientData, false);
}

static void LTDriverImageSensorJXF51Impl_NoImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *eventProc) {
    JXF51_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;
    s_event->UnregisterFromEvent(instance->hEvent, eventProc);
}

static void
LTDriverImageSensorJXF51Impl_PowerOn(LTDeviceUnit hUnit) {
    JXF51_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;

    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    iPinBank->Set(instance->hResetPin, 1);
    iPinBank->Set(instance->hPowerdownPin, 1);

    // Enable EXTCLK output to sensor and wait 4ms before talking over I2C per the datasheet
    s_libMipi->EnableExtclk(instance->hMipiBus, true);

    instance->state = JXF51_State_PowerUpDelay;
    s_thread->SetTimer(s_thread->GetCurrentThread(), LTTime_Milliseconds(4), PowerOnTimerProc, NULL, instance);
}

static void
LTDriverImageSensorJXF51Impl_PowerOff(LTDeviceUnit hUnit) {
    return;
    JXF51_Instance *instance = InstanceFromHandle(hUnit);
    if (!instance) return;

    s_libMipi->EnableExtclk(instance->hMipiBus, false);

    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    iPinBank->Set(instance->hResetPin, 0);
    iPinBank->Set(instance->hPowerdownPin, 0);
}

// refer to ak_sensor_update_a_gain_func
static void UpdateAGain(JXF51_Instance *instance, const int a_gain) {

    // From JX-F51 datasheet:
    // Programmable gain, valid 00 to 3F, Total gain = 2^PGA[6:4]*(1+PGA[3:0]/16)
    // So PGA[6:4] is coarse gain, and PGA[3:0] is fine gain.
    // But the register is only valid for 0x00 to 0x3f, 
    // so the gain is limited to 2^CG * (1 + FG / 16)
    // Minimum gain is 0:
    // Maximum gain is 0x3f: CG = 0x03, FG = 0x0f
    //  => 2^3 * (1 + 15 / 16) = 8 * (1 + 0.9375) = 8 * 1.9375 = 15.5

    // Datasheet is unclear, this should be tested. 
    // It is also unclean if attenuation is possible in the 0-1 range, i.e. if there is an implicit gain of 1.

    u32 m = 0;
    u32 n = 0;
    u32 targetGain = (u32)(a_gain);
    u32 oGain = 1;
    u8 gainValue;
    u8 r0x6aValue;

    // Convert input to the range of 0 to 63
    // input has a floor of 256

    targetGain >>= 8; // convert to 1/256x gain
    if (targetGain) targetGain--;
    if (targetGain > 63) {
        targetGain = 63; // maximum gain is 63/256
    }

    while (oGain < targetGain) {
        oGain <<= 1;
        m++;
    }

    n = targetGain - (oGain >> 1);

    gainValue = ((m << 4) | n);
    WriteRegister(instance, 0x0, gainValue);

    // Read back the value
    gainValue = ReadRegOneByte(instance, 0x0);
    r0x6aValue = ReadRegOneByte(instance, 0x6a);
    r0x6aValue &= ~0x70;
    if (gainValue < 0x20) {
        r0x6aValue |= 0x10;
    } else {
        r0x6aValue |= 0x40;
    }
    WriteRegister(instance, 0x6a, r0x6aValue);
}

// refer to ak_sensor_update_exp_time_func
static int UpdateExpTimeFunc(JXF51_Instance *instance, u32 expTime) {
    /*
     * Exposure line MSBs, EXP[15:8].; AEC[15:8]
     * Exposure time is defined by EXP[15:0] at line period base.
     * TEXP=EXP[15:0]*TLine
     * 
     * Reference driver limits exposure time to FrameH - 4
     */

    u32 limit = s_FPSToFrameTime[instance->fpsInfo.current_fps].frameTimeH - kSensorExposureTimeLimit;

    if (expTime > limit) {
        LTLOG_YELLOWALERT("exp.time", "Exposure time %lu exceeds limit %lu, clamping to limit", LT_Pu32(expTime), LT_Pu32(limit));
        expTime = limit;
    }

    // update exposure time
    WriteRegister(instance, 0x02, (expTime >> 8) & 0xFF);    // EXP[15:8]   (0x02)
    WriteRegister(instance, 0x01, expTime & 0xff);           // EXP[7:0]    (0x01)

    return 1; // EXP_EFFECT_FRAMES;
}

// refer to ak_sensor_init_func
static void InitSensorParams(JXF51_Instance *instance) {
    for (int i = 0; i < s_SensorSettingCount; ++i) {
        LTLOG_DEBUG("init.sensor", "addr=0x%02x (), value=0x%02x", s_SensorSettings[i].addr, s_SensorSettings[i].value);
        WriteRegister(instance, s_SensorSettings[i].addr, s_SensorSettings[i].value);
    }
    instance->r0x27_value = ReadRegOneByte(instance, 0x27);   // 0x3227
    instance->fpsInfo.current_fps = kSensorMaxFPS;
    instance->fpsInfo.to_fps = instance->fpsInfo.current_fps;
}

// refer to ak_sensor_set_fps_func
static void SetFps(JXF51_Instance *instance, u32 fps) {
    if (fps <= 0 || fps > kSensorMaxFPS) {
        LTLOG_YELLOWALERT("set.fps", "Invalid FPS %lu", LT_Pu32(fps));
        return;
    }

    u16 hval = s_FPSToFrameTime[fps].frameTimeH;
    u16 wval = s_FPSToFrameTime[fps].frameTimeW;

    LTLOG("set.fps", "FPS: %lu, W -> (%d)%04X H => (%d)%04X", LT_Pu32(fps), wval, wval, hval, hval);
    WriteRegister(instance, 0x23, hval >> 8);
    WriteRegister(instance, 0x22, hval & 0xFF);
    WriteRegister(instance, 0x21, wval >> 8);
    WriteRegister(instance, 0x20, wval & 0xFF);
}

static void GetFlipMirror(JXF51_Instance *instance, u8 *flags) {
    u8 r0x12_value = ReadRegOneByte(instance, 0x12);   // bit 5 = flip_en, bit 4 = mirror_en
    flags[0] = (r0x12_value >> 4) & 1; // flip_en (FlipV)
    flags[1] = (r0x12_value >> 5) & 1; // mirror_en (FlipH)
}

// refer to ak_sensor_set_flip_mirror
static void SetFlipMirror(JXF51_Instance *instance, bool bFlipV, bool bFlipH) {
    u8 r0x12_value = ReadRegOneByte(instance, 0x12) & ~(0x30);   // bit 5 = flip_en, bit 4 = mirror_en
    u8 r0x2c_value = 0;   // bit 1 = mirror_en

    // flip_en
    if (bFlipV) {
        r0x12_value |= 0x10;
    }

    // mirror_en
    if (bFlipH) {
        r0x12_value |= 0x20;
        r0x2c_value = 0x02;
    }

    WriteRegister(instance, 0x12, r0x12_value);   // FLIP_MIRROR_REG   (0x12)
    WriteRegister(instance, 0x2c, r0x2c_value);   // FLIP_MIRROR_REG_2 (0x2c)
}

static bool LTDriverImageSensorJXF51Impl_SetAttribute(LTDeviceUnit hUnit, LTImageSensorAttribute attr, const void *attrValue) {
    JXF51_Instance *instance = InstanceFromHandle(hUnit);
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
            u32 fps = *(u32 *)attrValue;
            LTLOG("set.fps", "FPS set %lu -> %lu", LT_Pu32(instance->fpsInfo.current_fps), LT_Pu32(fps));
            if (instance->fpsInfo.current_fps != fps) {
                SetFps(instance, fps);
            }
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

static void GetSensorStatus(JXF51_Instance *instance) {
    ILTDriverPins_BidirectionalBank *iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, instance->hResetPin);
    u32 rPin = iPinBank->Read(instance->hResetPin);
    u32 pPin = iPinBank->Read(instance->hPowerdownPin);
    LTLOG_SERVER("pin.status", "reset %lu pow %lu", LT_Pu32(rPin), LT_Pu32(pPin));
}

static bool LTDriverImageSensorJXF51Impl_GetAttribute(LTDeviceUnit hUnit, LTImageSensorAttribute attr, void *attrValue) {
    JXF51_Instance *instance = InstanceFromHandle(hUnit);
    switch (attr) {
        case kLTImageSensorAttribute_Id:
            //*(u16 *)attrValue = instance->sensorId;
            *(u16 *)attrValue = kSensorId; // 
            break;
        case kLTImageSensorAttribute_FrameRate:
            *(int *)attrValue = instance->fpsInfo.current_fps;
            break;

        case kLTImageSensorAttribute_ExposureForFps:
        {   // refer to ak_sensor_get_max_exp_for_fps
            LTImageSensorAttributeParams *param = attrValue;
            int fps = *(int *)param->in;
            int vts = s_FPSToFrameTime[fps].frameTimeH;
            if (vts <= 0) {
                LTLOG_YELLOWALERT("get.exp.fps", "Invalid FPS %d", fps);
                return false;
            }
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
    .OnImageSensorEvent = LTDriverImageSensorJXF51Impl_OnImageSensorEvent,
    .NoImageSensorEvent = LTDriverImageSensorJXF51Impl_NoImageSensorEvent,
    .PowerOn            = LTDriverImageSensorJXF51Impl_PowerOn,
    .PowerOff           = LTDriverImageSensorJXF51Impl_PowerOff,
    .SetAttribute       = LTDriverImageSensorJXF51Impl_SetAttribute,
    .GetAttribute       = LTDriverImageSensorJXF51Impl_GetAttribute,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverImageSensorJXF51, (ILTDriverImageSensor))

