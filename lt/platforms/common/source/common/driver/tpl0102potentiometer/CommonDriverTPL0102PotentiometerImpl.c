/*******************************************************************************
 * <common/source/common/driver/tpl0101potentiometer/CommonDriverTPL0102PotentiometerImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * TPL0102 I2C digital potentiometer driver
 ******************************************************************************/
/** @file CommonDriverTPL0102PotentiometerImpl.c Implementation of potentiometer
 *  driver
 */

#include <lt/core/LTCore.h>
#include <lt/core/LTMutex.h>

#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/potentiometer/LTDevicePotentiometer.h>

DEFINE_LTLOG_SECTION("tpl0102driver");

/*******************************************************************************
 * libraries
*******************************************************************************/
LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(CommonDriverTPL0102Potentiometer, (LTDeviceI2C, LTDeviceI2C));

/*******************************************************************************
 * typedefs
*******************************************************************************/
typedef enum {
    kTPL0102Reg_IVRA                = 0x00,             // Initial Value Register for Potentiometer A
    kTPL0102Reg_IVRB                = 0x01,             // Initial Value Register for Potentiometer B
    kTPL0102Reg_ACR                 = 0x10,             // Access Control Register
} PotentiometerReg;

typedef enum {
    kTPL0102ACR_WIP                 = 0x20,             // 0 = write not in progress, 1 = write in progress
    kTPL0102ACR_SHDN                = 0x40,             // 0 = shutdown enabled, 1 = shutdown disabled
    kTPL0102ACR_VOL                 = 0x80,             // 0 = non-volatile read/write, 1 = volatile read
} PotentiometerACRValue;

typedef struct {
    u32                             nI2CDeviceIndex;    // the device index in the I2C library
    u8                              nI2CAddress;        // the unit's I2C address
    u32                             nRegAddr;           // the address of the register to read/write DPOT value
} PotentiometerDeviceUnitInfo;

typedef struct {
    u32                             nDeviceIndex;       // index in the s_DeviceUnits array
    LTDeviceUnit                    hI2C;               // I2C device unit
} PotentiometerInstance;

/******************************************************************************
 * consts
******************************************************************************/
// device instances
#define POTENTIOMETER_DEVICE_UNIT(deviceIndex, addr, port) { deviceIndex, addr, port },
static PotentiometerDeviceUnitInfo s_DeviceUnits[]      = {
#include LT_STRINGIFY(CONFIGURATION_FILE)
};
#undef POTENTIOMETER_DEVICE_UNIT

static const u32                    kNumDeviceUnits     = sizeof(s_DeviceUnits) /
                                                          sizeof(PotentiometerDeviceUnitInfo);
// interface for creating device units
static const ILTDriverPotentiometer s_ILTDriverPotentiometer;

/*******************************************************************************
 * static variables
*******************************************************************************/
static ILTThread                  * s_iThread            = NULL;

/*******************************************************************************
 *
 * helper functions
 *
*******************************************************************************/

/*******************************************************************************
 * initializes the I2C bus
*******************************************************************************/
static bool InitializeI2C(LTDeviceUnit hI2C) {
    LTDeviceI2C_Capabilities caps;
    LTDeviceI2C_Configuration cfg;

    if (!LT_GetLTDeviceI2C()->GetDeviceCapabilities(hI2C, &caps)) {
        LTLOG("i2c.fail.init.caps", "handle %lx", LT_PLT_HANDLE(hI2C));
        return false;
    }
    cfg.Frequency   = caps.Freq_max;
    cfg.Master      = true;
    cfg.Async       = false;
    cfg.Dma         = false;
    bool bDeviceConfigured = LT_GetLTDeviceI2C()->SetDeviceConfiguration(hI2C, &cfg);
    if (!bDeviceConfigured) {
        LTLOG_YELLOWALERT("i2c.fail.init", "configuration");
    }
    return bDeviceConfigured;
}

/*******************************************************************************
 * Waits for WIP to clear and returns true if it did; false otherwise
 * *******************************************************************************/
static bool WaitForWIP(const PotentiometerInstance * pInstance) {
    bool bWIP                           = true;
    LTDevicePotentiometerResult eResult = kLTDevicePotentiometer_Success;

    // WIP gets cleared in about 2ms to 4ms in most cases; however, it takes longer on reset, so give it up to 100 iterations to make sure it's covered in all cases
    PotentiometerReg reg = kTPL0102Reg_ACR;
    for (u8 i = 0; i < 100 && eResult == kLTDevicePotentiometer_Success && bWIP; ++i) {
        u8 nACRVal = 0;
        if (!LT_GetLTDeviceI2C()->I2CMasterTransfer(pInstance->hI2C, s_DeviceUnits[pInstance->nDeviceIndex].nI2CAddress, &nACRVal, 1, &reg, 1, true, true, NULL, NULL)) {
           eResult = kLTDevicePotentiometer_ReadError;
        } else {
            bWIP = (nACRVal & kTPL0102ACR_WIP);
            if (bWIP) {
                LTLOG_DEBUG("potentiometer.wip", "write is in progress (ACR = 0x%02X)", nACRVal);
                s_iThread->Sleep(LTTime_Milliseconds(1));
            }
        }
    }

    return !bWIP;
}

/*******************************************************************************
 * reads the given register from the device
*******************************************************************************/
static LTDevicePotentiometerResult ReadReg(const PotentiometerInstance * pInstance, PotentiometerReg regAddr, u8 * data) {
    LTDevicePotentiometerResult eResult = kLTDevicePotentiometer_Success;
    if (WaitForWIP(pInstance)) {
        if (!LT_GetLTDeviceI2C()->I2CMasterTransfer(pInstance->hI2C, s_DeviceUnits[pInstance->nDeviceIndex].nI2CAddress, data, 1, &regAddr, 1, true, true, NULL, NULL)) {
            eResult = kLTDevicePotentiometer_ReadError;
        }
    } else {
        eResult = kLTDevicePotentiometer_DeviceBusy;
    }

    return eResult;
}

/*******************************************************************************
 * writes the given register to the device
*******************************************************************************/
static LTDevicePotentiometerResult WriteReg(const PotentiometerInstance * pInstance, u8 regAddr, u8 data) {
    u8 nACRVal                          = 0;
    LTDevicePotentiometerResult eResult = kLTDevicePotentiometer_Success;

    // check the WIP value in the control register to make sure values can be written
    if (WaitForWIP(pInstance)) {
        // make sure the control register is set for non-volatile access
        u8 ctrlCmd[2] = { kTPL0102Reg_ACR, (nACRVal & ~kTPL0102ACR_VOL) };
        if (!LT_GetLTDeviceI2C()->I2CMasterTransfer(pInstance->hI2C, s_DeviceUnits[pInstance->nDeviceIndex].nI2CAddress, NULL, 0, ctrlCmd, sizeof(ctrlCmd), true, true, NULL, NULL)) {
            LTLOG_YELLOWALERT("i2c.fail.write.ctl", "Could not write to control register");
            eResult = kLTDevicePotentiometer_WriteError;
        } else {
            u8 cmd[2] = { regAddr, data };
            if (!LT_GetLTDeviceI2C()->I2CMasterTransfer(pInstance->hI2C, s_DeviceUnits[pInstance->nDeviceIndex].nI2CAddress, NULL, 0, cmd, sizeof(cmd), true, true, NULL, NULL)) {
                LTLOG_YELLOWALERT("i2c.fail.write.cmd", "Cannot write I2C data");
                eResult = kLTDevicePotentiometer_WriteError;
            }
        }
    } else {
        LTLOG_YELLOWALERT("i2c.fail.write.wip", "Cannot write I2C data because write is in progress");
        eResult = kLTDevicePotentiometer_DeviceBusy;
    }

    return eResult;
}

/*******************************************************************************
 *
 * interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * library initialize function
*******************************************************************************/
static bool CommonDriverTPL0102PotentiometerImpl_LibInit(void) {
    u32 nI2CNumDeviceUnits = LT_GetLTDeviceI2C()->GetNumDeviceUnits();
    for (u32 i = 0; i < kNumDeviceUnits; ++i) {
        if (s_DeviceUnits[i].nI2CDeviceIndex >= nI2CNumDeviceUnits) {
            LTLOG_YELLOWALERT("deviceunits.fail", "I2C device index %lu is invalid", LT_Pu32(s_DeviceUnits[i].nI2CDeviceIndex));
            return false;
        }
    }

    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    return true;
}

/*******************************************************************************
 * library finalize function
*******************************************************************************/
static void CommonDriverTPL0102PotentiometerImpl_LibFini(void) {
    s_iThread = NULL;
}

/*******************************************************************************
 * Sets the value of the DPOT register for the given device unit
*******************************************************************************/
static LTDevicePotentiometerResult CommonDriverTPL0102PotentiometerImpl_SetDPOTValue(LTDeviceUnit hDeviceUnit, u32 value) {
    PotentiometerInstance * pInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    LTDevicePotentiometerResult eResult = (pInstance != NULL) ? kLTDevicePotentiometer_Success : kLTDevicePotentiometer_InvalidHandle;
    if (eResult == kLTDevicePotentiometer_Success) {
        // read the DPOT value and check if it needs updating
        u8 val = 0;
        eResult = ReadReg(pInstance, s_DeviceUnits[pInstance->nDeviceIndex].nRegAddr, &val);
        if (eResult != kLTDevicePotentiometer_Success) {
            LTLOG_YELLOWALERT("sensor.fail.dpot.read", "Failed to read DPOT value");
        } else {
            if (val != value) {
                eResult = WriteReg(pInstance, s_DeviceUnits[pInstance->nDeviceIndex].nRegAddr, (u8)value);
                if (eResult != kLTDevicePotentiometer_Success) {
                    LTLOG_YELLOWALERT("sensor.fail.dpot.update", "Failed to update DPOT value");
                }
                // check that the value was set correctly
                eResult = ReadReg(pInstance, s_DeviceUnits[pInstance->nDeviceIndex].nRegAddr, &val);
                if (eResult != kLTDevicePotentiometer_Success) {
                    LTLOG_YELLOWALERT("sensor.fail.dpot.read.check", "Failed to read DPOT value");
                } else {
                    // if the value does not match, then the device must be busy
                    eResult = (val != value ? kLTDevicePotentiometer_DeviceBusy : eResult);
                }
            }
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pInstance);
    }

    return eResult;
}

/*******************************************************************************
 * gets the resistance output for the given port
*******************************************************************************/
static u32 CommonDriverTPL0102PotentiometerImpl_GetDPOTValue(LTDeviceUnit hDeviceUnit) {
    u8 dpot = 0;
    // read the port and check if it need updating
    PotentiometerInstance * pInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
    if (pInstance != NULL) {
        if (ReadReg(pInstance, s_DeviceUnits[pInstance->nDeviceIndex].nRegAddr, &dpot) != kLTDevicePotentiometer_Success) {
            LTLOG_YELLOWALERT("sensor.fail.dpot.get", "Failed to read DPOT value");
        }
        LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pInstance);
    }

    return dpot;
}

/*******************************************************************************
 * number of available device units
*******************************************************************************/
static u32 CommonDriverTPL0102PotentiometerImpl_GetNumDeviceUnits(void) {
    return kNumDeviceUnits;
}

/*******************************************************************************
 * Provide a Device Unit handle
 *******************************************************************************/
static LTDeviceUnit CommonDriverTPL0102PotentiometerImpl_CreateDeviceUnitHandle(u32 nDeviceIndex) {
    LTDeviceUnit hDeviceUnit = 0;
    bool bSuccess = true;
    if (nDeviceIndex >= kNumDeviceUnits) {
        LTLOG_YELLOWALERT("device.fail.index.out-of-range", "invalid potentiometer device index %d", (int)nDeviceIndex);
    } else {
        // Potentiometer DeviceUnit
        hDeviceUnit = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverPotentiometer, sizeof(PotentiometerInstance));
        bSuccess = (hDeviceUnit != 0);
        PotentiometerInstance * pInstance = NULL;
        if (bSuccess) {
            bSuccess = (pInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit));
        }
        // I2C DeviceUnit
        if (bSuccess) {
            pInstance->nDeviceIndex = nDeviceIndex;
            bSuccess = (pInstance->hI2C = LT_GetLTDeviceI2C()->CreateDeviceUnitHandle(s_DeviceUnits[nDeviceIndex].nI2CDeviceIndex));
        }
        // init I2C
        if (bSuccess) {
            bSuccess = InitializeI2C(pInstance->hI2C);
        }
        if (bSuccess) {
            // make sure there's a valid device
            bSuccess = LT_GetLTDeviceI2C()->ProbeAddress(pInstance->hI2C, s_DeviceUnits[nDeviceIndex].nI2CAddress);
            if (!bSuccess) {
                LTLOG_YELLOWALERT("potentiometer.fail.instance", "Potentiometer device not detected");
            }
        }

        if (pInstance) {
            LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pInstance);
        }

        if (!bSuccess && hDeviceUnit != 0) {
            LT_GetCore()->DestroyHandle(hDeviceUnit);
            hDeviceUnit = 0;
        }
    }

    return hDeviceUnit;
}

/*******************************************************************************
 * cleanup Device Unit handle
 *******************************************************************************/
static void OnDestroyHandle(LTHandle hDeviceUnit) {
    if (hDeviceUnit != 0) {
        PotentiometerInstance * pInstance = LT_GetCore()->ReserveHandlePrivateData(hDeviceUnit);
        if (pInstance != NULL) {
            if (pInstance->hI2C != 0) {
                LT_GetCore()->DestroyHandle(pInstance->hI2C);
            }
            LT_GetCore()->ReleaseHandlePrivateData(hDeviceUnit, pInstance);
        }
    }
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDevicePotentiometer, CommonDriverTPL0102Potentiometer);

LTLIBRARY_EXPORT_INTERFACES(CommonDriverTPL0102Potentiometer, (ILTDriverPotentiometer))

define_LTLIBRARY_INTERFACE(ILTDriverPotentiometer, OnDestroyHandle)
    .SetDPOTValue                    = CommonDriverTPL0102PotentiometerImpl_SetDPOTValue,
    .GetDPOTValue                    = CommonDriverTPL0102PotentiometerImpl_GetDPOTValue
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Jan-22   vitellius   created
 */
