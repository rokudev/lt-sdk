/*******************************************************************************
 * platforms/common/source/common/driver/BL612pir/CommonDriverPIRBL612Impl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Common LT Driver Library for BL612 PIR sensor access
 *
 ******************************************************************************/
#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>

#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/potentiometer/LTDevicePotentiometer.h>

#include <lt/device/pir/LTDevicePIRDefs.h>
#include <lt/device/pir/LTDevicePIR.h>

DEFINE_LTLOG_SECTION("lt.drv.bl612pir");

/*******************************************************************************
 * macros
******************************************************************************/
LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(CommonDriverPIRBL612, (LTDevicePins, LTDevicePins) (LTDevicePotentiometer, LTDevicePotentiometer));

/*******************************************************************************
 * typedefs
*******************************************************************************/
typedef struct {
    u16                             nOnTime;    // ON-TIME delay in seconds
    u8                              nDPOTValue; // the DPOT value for the given time
} PIR612OnTimeValue;

typedef struct {
    const char                    * pDeviceName;
    const char                    * pPinName;
    const u32                       nSenseDeviceIndex;
    const u32                       nOnTimeDeviceIndex;
    LTDeviceUnit                    hPin;
    LTDeviceUnit                    hSenseDevice;
    LTDeviceUnit                    hOnTimeDevice;
} BL612DeviceInfo;

/******************************************************************************
 * consts
******************************************************************************/
// supported time delays (ON-TIME)
// The table below is data points collected through looking over the datasheet and applying the values to achieve
// the desired time delays.
// The equation for calculating the values is a logarithmic regression equation; however, that was not used here
// because the calculated equation did not fit the tested results at small values and LT does not have a math library
// with a log() function yet. This also avoids floating-point math
static const PIR612OnTimeValue      kTimeOnTimeValues[]         = {
    {   2,   0 }, {   5,   10 }, {   10,  20 }, {   15,  30 },
    {  20,  40 }, {  30,   55 }, {   45,  70 }, {   60,  85 },
    {  90, 100 }, { 120,  115 }, {  180, 130 }, {  300, 145 },
    { 600, 175 }, { 900,  205 }, { 1800, 235 }, { 3600, 255 }
};

// define the device struct array
#define DEFINE_BL612_DEVICE(index, pinName, senseDeviceIndex, ontimeDeviceIndex)    \
    { .pDeviceName = "BL612-"#index, .pPinName = pinName, .nSenseDeviceIndex = senseDeviceIndex, .nOnTimeDeviceIndex = ontimeDeviceIndex },
static BL612DeviceInfo s_PIRDevices[]                           = {
#include LT_STRINGIFY(CONFIGURATION_FILE)
};
#undef DEFINE_BL612_DEVICE

static const LT_SIZE                kNumDeviceUnits             = sizeof(s_PIRDevices) / sizeof(BL612DeviceInfo);

/*******************************************************************************
 * static variables
*******************************************************************************/
// interfaces
static ILTThread                  * s_iLTThread                 = NULL;
static ILTDriverPins_InputBank    * s_iDriverInputBank          = NULL;
static ILTDriverPotentiometer     * s_iDriverPotentiometer      = NULL;

static LTThread                     s_hNotificationThread       = 0;
static LTThread_TaskProc          * s_pMotionTaskProc           = NULL;

static u8                           s_nDriverId                 = 0;

/*******************************************************************************
 *
 * PIR device helper functions
 *
*******************************************************************************/

/*******************************************************************************
 * sensor pin IRQ
 *******************************************************************************/
static void PinISR(bool bPinHigh, void * pClientData) {
    u32 nDeviceIndex = (u32)((LT_SIZE)pClientData);
    if (s_hNotificationThread != 0 && s_pMotionTaskProc != NULL) {
        LTDevicePIRMotionEvent event = { s_nDriverId, nDeviceIndex, bPinHigh, 0 };
        u32 u32Event = (u32)(*(u32 *)(&event));
        s_iLTThread->QueueTaskProc(s_hNotificationThread, s_pMotionTaskProc, NULL, (void *)u32Event);
    }
}

/*******************************************************************************
 * Frees handles and libraries
 *******************************************************************************/
static void Cleanup(void) {
    for (u32 i = 0; i < kNumDeviceUnits; ++i) {
        if (s_PIRDevices[i].hPin != 0) {
            if (s_iDriverInputBank != NULL) {
                s_iDriverInputBank->DisableIRQ(s_PIRDevices[i].hPin);
            }
            LT_GetLTDevicePins()->Destroy(s_PIRDevices[i].hPin);
            s_PIRDevices[i].hPin = 0;
        }

        if (s_iDriverPotentiometer != NULL) {
            if (s_PIRDevices[i].hSenseDevice != 0) {
                s_iDriverPotentiometer->Destroy(s_PIRDevices[i].hSenseDevice);
                s_PIRDevices[i].hSenseDevice = 0;
            }
            if (s_PIRDevices[i].hOnTimeDevice != 0) {
                s_iDriverPotentiometer->Destroy(s_PIRDevices[i].hOnTimeDevice);
                s_PIRDevices[i].hOnTimeDevice = 0;
            }
        }
    }

    s_hNotificationThread   = 0;
    s_pMotionTaskProc       = NULL;
    s_iLTThread             = NULL;
    s_iDriverInputBank      = NULL;
    s_iDriverPotentiometer  = NULL;
}

/*******************************************************************************
 *
 * driver interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * driver lib init
 *******************************************************************************/
static bool CommonDriverPIRBL612Impl_LibInit(void) {
    s_iLTThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
    if (s_iLTThread == NULL) {
        LTLOG_REDALERT("fail.thread.interface", "Could not get ILTThread interface");
        Cleanup();
        return false;
    }

    for (u32 i = 0; i < kNumDeviceUnits; ++i) {
        u32 bankId = 0;
        if (!LT_GetLTDevicePins()->GetUnitNumberFromBankName(s_PIRDevices[i].pPinName, &bankId)) {
            LTLOG_REDALERT("fail.pin.bank.number", "Could not get pin bank number for pin \"%s\"", s_PIRDevices[i].pPinName);
            Cleanup();
            return false;
        }
        s_PIRDevices[i].hPin = LT_GetLTDevicePins()->CreateDeviceUnitHandle(bankId);
        if (s_PIRDevices[i].hPin == 0) {
            LTLOG_REDALERT("fail.pin.deviceunit", "Failed to create input pin device unit");
            Cleanup();
            return false;
        }

        s_PIRDevices[i].hSenseDevice = LT_GetLTDevicePotentiometer()->CreateDeviceUnitHandle(s_PIRDevices[i].nSenseDeviceIndex);
        if (s_PIRDevices[i].hSenseDevice == 0) {
            LTLOG_REDALERT("fail.sense.deviceunit", "Failed to create device unit for SENSE");
            Cleanup();
            return false;
        }
        s_PIRDevices[i].hOnTimeDevice = LT_GetLTDevicePotentiometer()->CreateDeviceUnitHandle(s_PIRDevices[i].nOnTimeDeviceIndex);
        if (s_PIRDevices[i].hOnTimeDevice == 0) {
            LTLOG_REDALERT("fail.ontime.deviceunit", "Failed to create device unit for ON-TIME");
            Cleanup();
            return false;
        }
    }

    s_iDriverInputBank = lt_gethandleinterface(ILTDriverPins_InputBank, s_PIRDevices[0].hPin);
    if (s_iDriverInputBank == NULL) {
        LTLOG_REDALERT("fail.pin.interface", "Failed to get pin interface");
        Cleanup();
        return false;
    }

    s_iDriverPotentiometer = lt_gethandleinterface(ILTDriverPotentiometer, s_PIRDevices[0].hSenseDevice);
    if (s_iDriverPotentiometer == NULL) {
        LTLOG_REDALERT("fail.potentiometer.interface", "Failed to the potentiometer driver interface");
        Cleanup();
        return false;
    }

    return true;
}

/*******************************************************************************
 * driver lib fini
 *******************************************************************************/
static void CommonDriverPIRBL612Impl_LibFini(void) {
    Cleanup();
}

/*******************************************************************************
 * Enable/disable the driver by enabling/disabling IRQ handler
 *******************************************************************************/
static void CommonDriverPIRBL612Impl_Enable(u32 nDeviceIndex, bool bEnable) {
    if (nDeviceIndex < kNumDeviceUnits) {
        if (bEnable) {
            s_iDriverInputBank->EnableIRQ(s_PIRDevices[nDeviceIndex].hPin, kLTDevicePin_PinConfiguration_Trigger_BothEdges, LTTime_Zero(), PinISR, (void *)nDeviceIndex);
        } else {
            s_iDriverInputBank->DisableIRQ(s_PIRDevices[nDeviceIndex].hPin);
        }
    }
}

/*******************************************************************************
 * Sets the sensitivity for movement detection
 *******************************************************************************/
static bool CommonDriverPIRBL612Impl_SetSensitivity(u32 nDeviceIndex, u8 nPercent) {
    // the range of the DPOT value is 255 to 0
    u32 nDPOTValue = -2.55 * nPercent + 255;
    LTLOG_DEBUG("dpot.range.value", "Setting DPOT value to 0x%02lX", LT_Pu32(nDPOTValue));
    LTDevicePotentiometerResult eResult = s_iDriverPotentiometer->SetDPOTValue(s_PIRDevices[nDeviceIndex].hSenseDevice, nDPOTValue);
    if (eResult == kLTDevicePotentiometer_DeviceBusy) {
        // try again
        eResult = s_iDriverPotentiometer->SetDPOTValue(s_PIRDevices[nDeviceIndex].hSenseDevice, nDPOTValue);
        if (eResult != kLTDevicePotentiometer_Success) {
            LTLOG_YELLOWALERT("fail.range.value", "Failed to set the SENSE value (error: %d)", eResult);
        }
    }

    return (eResult == kLTDevicePotentiometer_Success);
}

/*******************************************************************************
 * Sets the interval where no motion is re-detected to avoid flip-flopping
 *******************************************************************************/
static bool CommonDriverPIRBL612Impl_SetMotionEndDelay(u32 nDeviceIndex, LTTime interval) {
    LT_UNUSED(interval);
    u32 seconds = LTTime_GetSeconds(interval);
    u8 index = LT_U8_MAX;
    LTDevicePotentiometerResult eResult = kLTDevicePotentiometer_DeviceBusy;
    // get the index in the time array
    for (s8 i = (sizeof(kTimeOnTimeValues) / sizeof(PIR612OnTimeValue)) - 1; i >= 0 && index == LT_U8_MAX; --i) {
        if (kTimeOnTimeValues[i].nOnTime == seconds || i == 0) {
            // reached the first one, so use that one
            index = i;
        } else if (seconds > kTimeOnTimeValues[i - 1].nOnTime) {
            u16 halfWay = kTimeOnTimeValues[i - 1].nOnTime + ((kTimeOnTimeValues[i].nOnTime - kTimeOnTimeValues[i - 1].nOnTime) / 2);
            index = (seconds >= halfWay) ? i : i - 1;
        }
    }
    if (index == LT_U8_MAX) {
        LTLOG_YELLOWALERT("fail.time.index", "Failed to find the index in the time array");
    } else {
        LTLOG_DEBUG("dpot.on-time.value", "Setting DPOT value to 0x%02lX (time index %d)", LT_Pu32(kTimeOnTimeValues[index].nDPOTValue), index);
        eResult = s_iDriverPotentiometer->SetDPOTValue(s_PIRDevices[nDeviceIndex].hOnTimeDevice, kTimeOnTimeValues[index].nDPOTValue);
        if (eResult == kLTDevicePotentiometer_DeviceBusy) {
            // try again
            eResult = s_iDriverPotentiometer->SetDPOTValue(s_PIRDevices[nDeviceIndex].hOnTimeDevice, kTimeOnTimeValues[index].nDPOTValue);
            if (eResult != kLTDevicePotentiometer_Success) {
                LTLOG_YELLOWALERT("fail.time.value", "Failed to set the ON-TIME value");
            }
        }
    }

    return (eResult == kLTDevicePotentiometer_Success);
}

/*******************************************************************************
 * Starts reporting motion to the given TaskProc
 *******************************************************************************/
static void CommonDriverPIRBL612Impl_StartMotionDetection(LTThread_TaskProc * pMotionTaskProc, u8 nDriverId) {
    s_hNotificationThread   = s_iLTThread->GetCurrentThread();
    s_pMotionTaskProc       = pMotionTaskProc;
    s_nDriverId             = nDriverId;
}

/*******************************************************************************
 * Starts reporting motion
 *******************************************************************************/
static void CommonDriverPIRBL612Impl_StopMotionDetection(void) {
    s_hNotificationThread = 0;
    s_pMotionTaskProc = NULL;
}

/*******************************************************************************
 * returns the driver's name
 *******************************************************************************/
static const char * CommonDriverPIRBL612Impl_GetDeviceName(u32 nDeviceIndex) {
    const char * pDeviceName = NULL;
    if (nDeviceIndex < kNumDeviceUnits) {
        pDeviceName = s_PIRDevices[nDeviceIndex].pDeviceName;
    }
    return pDeviceName;
}

/*******************************************************************************
 * returns the number of device units
 *******************************************************************************/
static u32 CommonDriverPIRBL612Impl_GetNumDeviceUnits(void) {
    return kNumDeviceUnits;
}

/*******************************************************************************
 * create device unit
 *******************************************************************************/
static LTDeviceUnit CommonDriverPIRBL612Impl_CreateDeviceUnitHandle(u32 nDeviceUnitNum) {
    LT_UNUSED(nDeviceUnitNum);
    // no device unit created by the driver
    return 0;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverPIR, CommonDriverPIRBL612);

LTLIBRARY_EXPORT_INTERFACES(CommonDriverPIRBL612, (ILTDriverPIR))

define_LTLIBRARY_INTERFACE(ILTDriverPIR) {
    .GetDeviceName                      = CommonDriverPIRBL612Impl_GetDeviceName,
    .Enable                             = CommonDriverPIRBL612Impl_Enable,
    .SetSensitivity                     = CommonDriverPIRBL612Impl_SetSensitivity,
    .SetMotionEndDelay                  = CommonDriverPIRBL612Impl_SetMotionEndDelay,
    .StartMotionDetection               = CommonDriverPIRBL612Impl_StartMotionDetection,
    .StopMotionDetection                = CommonDriverPIRBL612Impl_StopMotionDetection
} LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Feb-22   vitellius   created
 */
