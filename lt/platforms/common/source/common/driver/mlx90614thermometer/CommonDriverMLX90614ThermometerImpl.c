/*******************************************************************************
 * <common/source/common/driver/mlx90614thermometer/CommonDriverMLX90614ThermometerImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * MLX90614 thermometer driver
 ******************************************************************************/
/** @file CommonDriverMLX90614ThermometerImpl.c Implementation of temperature sensor
 *  driver
 */

#include <lt/core/LTCore.h>
#include <lt/device/thermometer/LTDeviceThermometer.h>
#include <lt/device/i2c/LTDeviceI2C.h>

DEFINE_LTLOG_SECTION("mlx90614driver");
static ILTThread       *              s_iThread    = NULL;
static LTDeviceI2C     *              s_pDeviceI2C = NULL;
static const ILTThermometerDeviceUnit s_ILTThermometerDeviceUnit;

enum {
    k_MLX90614_Reg_Ram_Raw_IR_1       = 0x04, /* Raw IR channel 1 */
    k_MLX90614_Reg_Ram_Raw_IR_2       = 0x05, /* Raw IR channel 2 */
    k_MLX90614_Reg_Ram_Temp_Ambient   = 0x06, /* Ambient temperature */
    k_MLX90614_Reg_Ram_Temp_Obj1      = 0x07, /* Object1 temperature */
    k_MLX90614_Reg_Ram_Temp_Obj2      = 0x08, /* Object2 temperature */
};

#define MLX90614_RAM_READ_CMD(reg) ((u8)reg)
#define MLX90614_EEPROM_READ_CMD(reg) ((u8)reg | (1<<5))
/***********************************************************************************************************************
 * Temperature sensor instance data                                                                                   */
typedef struct {
    u8            const Address;        /**< I2C address of temperature sensor */
    LTMutex      *mutex;                /**< Mutex protection, mainly for reference count */
    u32           nRefCount;            /**< How many clients have an LTHandle to this instance */
    LTDeviceUnit  hI2C;
} ThermometerInstance;

static ThermometerInstance s_ThermometerInstance[] = {
    {
        .Address         = 0x5A,
    }
};

/***********************************************************************************************************************
 * Number of available temperature sensors on this platform                                                           */
enum { kNumThermometerDeviceUnits = sizeof(s_ThermometerInstance) / sizeof (ThermometerInstance) };
static u32 nNumThermometerDevicesFound = 0;

static bool InitializeI2C(LTDeviceUnit hI2C);

static bool ProbeMLX90614ThermometerInstance(ThermometerInstance *pInstance, u32 indexDev, LTDeviceUnit hI2C) {
    LT_UNUSED(indexDev);
    bool bResult = InitializeI2C(hI2C);
    if (bResult) {
        bResult = s_pDeviceI2C->ProbeAddress(hI2C, pInstance->Address);
        LTLOG("probe.result", "Index %lu I2C handle %lx, probe=%d", indexDev, LT_PLT_HANDLE(hI2C), (bResult)?(1):(0));
    }
    return (bResult);
}
/***********************************************************************************************************************
 * Library initialization upon opening                                                                                */
static void CommonDriverMLX90614ThermometerImpl_LibFini(void) {
    if (s_pDeviceI2C) {
        lt_closelibrary(s_pDeviceI2C);
        s_pDeviceI2C = NULL;
        nNumThermometerDevicesFound = 0;
    }
    ThermometerInstance * pInstance = s_ThermometerInstance;
    for (int nIndex = 0; nIndex < kNumThermometerDeviceUnits; ++nIndex, ++pInstance) {
        LT_ASSERT(pInstance->nRefCount == 0);
        if (pInstance->mutex) {
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
    }
}

static bool CommonDriverMLX90614ThermometerImpl_LibInit(void) {
    s_pDeviceI2C = (LTDeviceI2C *)LT_GetCore()->OpenLibrary("LTDeviceI2C");
    if (!s_pDeviceI2C) {
        return false;
    }
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    u32 nNumDeviceUnits = s_pDeviceI2C->GetNumDeviceUnits();
    if (nNumDeviceUnits == 0) {
        LTLOG_YELLOWALERT("fail.deviceunits", "LTDeviceI2C provides no Device Units");
        return false;
    } else {
        for (u32 n = 0; n < nNumDeviceUnits; n++) {
            LTDeviceUnit hI2C = s_pDeviceI2C->CreateDeviceUnitHandle(n);
            bool bI2CClaimed = false;
            ThermometerInstance * pInstance = s_ThermometerInstance;
            for (int nIndex = 0; nIndex < kNumThermometerDeviceUnits; ++nIndex, ++pInstance) {
                /* Initialize the ThermometerInstance: */
                if ( !pInstance->hI2C && ProbeMLX90614ThermometerInstance(pInstance, n, hI2C)) {
                    pInstance->hI2C = hI2C;
                    bI2CClaimed = true;
                    pInstance->nRefCount = 0;
                    nNumThermometerDevicesFound++;
                    if (!(pInstance->mutex = lt_createobject(LTMutex))) {
                        CommonDriverMLX90614ThermometerImpl_LibFini();
                        return false;
                    }
                    break;
                }
            }
            if (!bI2CClaimed && hI2C) {
                LT_GetCore()->DestroyHandle(hI2C);
            }

        }
    }
    if (nNumThermometerDevicesFound != kNumThermometerDeviceUnits) {
        LTLOG_YELLOWALERT("fail.probe", "thermometers not found!");
        return false;
    }
    LTLOG_DEBUG("libinit", "ok");
    return true;
}

static u32 CommonDriverMLX90614ThermometerImpl_GetNumDeviceUnits(void) { return nNumThermometerDevicesFound; }

static bool InitializeI2C(LTDeviceUnit hI2C) {
    LTDeviceI2C_Capabilities caps;
    LTDeviceI2C_Configuration cfg;
    if (s_pDeviceI2C->GetDeviceCapabilities(hI2C, &caps)) {
        LTLOG_DEBUG("i2c.init.caps", "handle %lx freq_min %lu freq_max %lu caps %lu", LT_PLT_HANDLE(hI2C), LT_Pu32(caps.Freq_min), LT_Pu32(caps.Freq_max), LT_Pu32(caps.Caps_mask));
    } else {
        LTLOG("i2c.init.err.caps", "handle %lx", LT_PLT_HANDLE(hI2C));
        return false;
    }
    cfg.Frequency = caps.Freq_max;
    cfg.Master = true;
    cfg.Async = false;
    cfg.Dma = false;
    bool bDeviceConfigured = s_pDeviceI2C->SetDeviceConfiguration(hI2C, &cfg);
    if (!bDeviceConfigured) {
        LTLOG("i2c.init.fail", "configuration");
    }
    return bDeviceConfigured;
}

/***********************************************************************************************************************
 * Provide a Device Unit handle.  Furnish the handle with a pointer to the respective instance data.
 * Initialize the I2C control block for the sensor if this is the first handle for the sensor.                        */
static LTDeviceUnit CommonDriverMLX90614ThermometerImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;
    LTLOG_DEBUG("create.handle", "unit %lu", nDeviceUnitNumber);
    if (nDeviceUnitNumber < kNumThermometerDeviceUnits) {
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTThermometerDeviceUnit, sizeof(ThermometerInstance *));
        if (hDevice) {
            ThermometerInstance ** ppInstance = (ThermometerInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
            if (ppInstance) {
                ThermometerInstance * pInstance = *ppInstance = &s_ThermometerInstance[nDeviceUnitNumber];
                pInstance->mutex->API->Lock(pInstance->mutex);
                u32 nRefCount = ++pInstance->nRefCount;
                pInstance->mutex->API->Unlock(pInstance->mutex);
                if (nRefCount == 1) {   /* Just starting up this instance. */
                    InitializeI2C(pInstance->hI2C);
                }
                LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
            } else {
                LT_GetCore()->DestroyHandle(hDevice);
            }
        }
    }
    return hDevice;
}

/***********************************************************************************************************************
 * Reclaim a Device Unit handle.  Retrieve the refcount, and shut down the I2C instance if there are no more refs.    */
static void CommonDriverMLX90614ThermometerDeviceUnit_OnDestroyHandle(LTHandle hDevice) {
    if (!hDevice) return;
    ThermometerInstance ** ppInstance = (ThermometerInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    if (!ppInstance) return;
    ThermometerInstance * pInstance = *ppInstance;
    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        if (pInstance->nRefCount > 0)
            --pInstance->nRefCount;
        if (pInstance->nRefCount == 0) {
            LT_GetCore()->DestroyHandle(pInstance->hI2C);
            pInstance->hI2C = 0;
        }
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
    LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
}

/***********************************************************************************************************************
 * Synchronous read of temperature sensor and conversion to degrees Milligrade                                        */
static s32 CommonDriverMLX90614ThermometerDeviceUnit_Read(LTDeviceUnit hDevice) {
    ThermometerInstance ** ppInstance = (ThermometerInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    if (!ppInstance) return kLTDeviceThermometerErrorTemperature;   /* invalid private data pointer */
    s32 temperature = kLTDeviceThermometerErrorTemperature;              /* typically due to an invalid instance pointer */
    ThermometerInstance * pInstance = *ppInstance;
    if (pInstance) {
        u8 temperatureData[3];
        u8 cmd = MLX90614_RAM_READ_CMD(k_MLX90614_Reg_Ram_Temp_Obj1);
        /* Write transaction - command "Read memory register": do not issue stop */
        bool bResult = s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C,pInstance->Address, NULL, 0, &cmd, sizeof(cmd), true, false, NULL, NULL);
        if (!bResult) {
            LTLOG_DEBUG("mlx.error.1", "Cannot write I2C data");
            return -5000;
        }
        /* Read temperature - from the memory register: issue repeated start and stop at the end of transaction */
        bResult = s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C,pInstance->Address, temperatureData, sizeof(temperatureData), NULL, 0, true, true, NULL, NULL);
        if (!bResult) {
            LTLOG_DEBUG("mlx.error.2", "Cannot read I2C data");
            return -5000;
        }
        if ((temperatureData[0] == temperatureData[1]) && (temperatureData[2] == 0xFF)  &&  (temperatureData[1] == temperatureData[2])) {
            LTLOG_DEBUG("mlx.error.3", "Bad temp data");
            return -5000;
        }
        //LTLOG_DEBUG("mlx.r1", "0x%x",  temperatureData[0]);
        //LTLOG_DEBUG("mlx.r2", "0x%x", temperatureData[1]);
        //LTLOG_DEBUG("mlx.pec", "0x%x", temperatureData[2]);
        /* Temp_C = (T[1] << 8 | T[0])* 0.02 - 273.15 */
        /* Convert to 10th of C */
        temperature = (((((s32)temperatureData[1]) << 8) | ((s32)temperatureData[0])) << 1) / 10 - 2732;
    }
    LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    return temperature;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceThermometer, CommonDriverMLX90614Thermometer);

define_LTLIBRARY_INTERFACE(ILTThermometerDeviceUnit, CommonDriverMLX90614ThermometerDeviceUnit_OnDestroyHandle) {
    .Read = CommonDriverMLX90614ThermometerDeviceUnit_Read,
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Jul-21   titus       created
 */
