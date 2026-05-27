/*******************************************************************************
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for Pan and Tilt Motor
 ******************************************************************************/
/** @file LTDriverMotorPanTiltMS320008N.c Implementation of Pan and Tilt Motor
 */

#include "motor.h"

#include <lt/core/LTCore.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("drv.motor.ms320008N")

typedef enum {
    kIrcutCtrl_Reset,
    kIrcutCtrl_Close,
    kIrcutCtrl_Open,
} IrcutCtrl;

typedef struct DeviceUnits {
    u32 *pControlBusIndex; /* Pointer into the heap where the Device Units are stored (as an array) */
    u32  nNumDeviceUnits;  /* How many Device Units this Driver supplies*/
} DeviceUnits;


static  LTDeviceI2C    *s_plibI2C;
static  LTDevicePins   *s_pdevPins;
static  LTDeviceUnit    s_hI2cBus;
static  DeviceUnits     s_DeviceUnits;
static  bool            s_resetState;

static void ReadRegister(u8 registerAddress, void *recvBuf, u32 recvBufSize) {
    s_plibI2C->I2CMasterTransfer(s_hI2cBus, kMotorSlaveMS320008NAddress, NULL, 0, &registerAddress, sizeof(u8), true, true, NULL, NULL);
    s_plibI2C->I2CMasterTransfer(s_hI2cBus, kMotorSlaveMS320008NAddress, recvBuf, recvBufSize, NULL, 0, true, true, NULL, NULL);
}

static bool WriteRegister(u8 registerAddress, u8 value) {
    u8 command[2] = { registerAddress, value,};
    if (s_plibI2C->I2CMasterTransfer(s_hI2cBus, kMotorSlaveMS320008NAddress, NULL, 0, command, sizeof(command), true, true, NULL, NULL)) {
        return true;
    }
    return false;
}

static bool MotorPanTiltIsRunning(u8 channel) {
    u8 data = 0;
    ReadRegister(kLTMotorCommmand_StatusReq + kMotorConfigureTiltChannel * channel, &data, sizeof(data));
    if ((data & kMotorCheckRunningFlag) > 0) return true;
    return false;
}

static bool MotorControl(u8 channel, u8 command, u8 value) {
    u8 dataCommand = command;
    if (channel == kLTMotorChannel_Tilt) {
        dataCommand += kMotorConfigureTiltChannel;
    }
    return WriteRegister(dataCommand, value);
}

static void MotorPanTiltSpeed(u32 motorSpeed) {
    motorSpeed = (kDefaultMotorSpeed << 1) - motorSpeed; // convert speed to frequency range
    MotorControl(kLTMotorChannel_Tilt, kLTMotorCommmand_SetFreqL, ((motorSpeed) & 0xFF));
    MotorControl(kLTMotorChannel_Tilt, kLTMotorCommmand_SetFreqH, (((motorSpeed) >> 8) & 0xFF));
    MotorControl(kLTMotorChannel_Pan, kLTMotorCommmand_SetFreqL, ((motorSpeed) & 0xFF));
    MotorControl(kLTMotorChannel_Pan, kLTMotorCommmand_SetFreqH, (((motorSpeed) >> 8) & 0xFF));
}

static void MotorPanTiltMove(MotorData *data) {
    u16  delta         = 0;
    u16  turnPulse     = 0;
    u16  remainPulse   = 0;
    LTLOG_DEBUG("motor.move", "X: %d Y: %d", data->deltaX, data->deltaY);
    for (int channel = 0; channel < kLTMotorChannel_PanTilt; channel++) {
        delta = (u16)lt_abs(channel ? data->deltaY : data->deltaX);      // Select deltaY if 'channel' is non-zero, otherwise deltaX, and take its absolute value
        turnPulse = (remainPulse > 0) ? remainPulse : delta;
        if (!MotorControl(channel, kLTMotorCommmand_SetMode, kMotorControlEnable)) return;
        if (!MotorControl(channel, kLTMotorCommmand_SetPulseL, turnPulse % kMS320008NPulseByteDivider)) return;
        if (!MotorControl(channel, kLTMotorCommmand_SetPulseH, (turnPulse >> 8))) return;
        if (!MotorControl(channel, kLTMotorCommmand_SetDir, (channel ? data->deltaY : data->deltaX) > 0 ? kMotorDirClockwise : kMotorDirCounterClockwise)) return;
        if (!MotorControl(channel, kLTMotorCommmand_SetAmp, kMotorSetCurrentAmplitude)) return;
        if (!MotorControl(kLTMotorChannel_PanTilt, kLTMotorCommmand_ConfLoad, (kMotorLoadConfiguration >> channel))) return;
    }
}

static void MotorPanTiltStop(void) {
    for (u8 channel = 0; channel < kLTMotorChannel_PanTilt; channel++) {
        if (!MotorControl(kLTMotorChannel_PanTilt, kLTMotorCommmand_ForceStop, (kMotorLoadConfiguration >> channel)))
            break;
    }
}

static u8 MotorPanTiltReset(motor_reset_info_s *reset) {
    MotorData data;
    data.deltaX = -MAX_PAN_MOTOR_PULSE;
    data.deltaY = -MAX_TILT_MOTOR_PULSE;
    s_resetState = true;
    MotorPanTiltMove(&data);
    reset->x_max_steps = MAX_PAN_MOTOR_PULSE;
    reset->y_max_steps = MAX_TILT_MOTOR_PULSE;
    return true;
}

bool IrcutOpen(void) {
    return WriteRegister(kLTMotorCommmand_DcCtrl, kIrcutCtrl_Open);
}

bool IrcutClose(void) {
    return WriteRegister(kLTMotorCommmand_DcCtrl, kIrcutCtrl_Close);
}

bool IrcutReset(void) {
    return WriteRegister(kLTMotorCommmand_DcCtrl, kIrcutCtrl_Reset);
}

static s32 MotorCommandHandler(s32 command, unsigned long arg) {
    switch (command) {
        case kMotorAction_Stop:
            {
                MotorPanTiltStop();
                break;
            }
        case kMotorAction_Reset:
            {
                motor_reset_info_s *reset = (motor_reset_info_s*)arg;
                MotorPanTiltReset(reset);
                break;
            }
        case kMotorAction_Move:
            {
                MotorPanTiltMove((MotorData *)arg);
                break;
            }
        case kMotorAction_GetStatus:
            {
                MotorStatus *mData = (MotorStatus*)arg;
                mData->status = kMotorOperationalStatus_Running;
                if (!MotorPanTiltIsRunning(kLTMotorChannel_Pan) && !MotorPanTiltIsRunning(kLTMotorChannel_Tilt)) {
                    mData->status = kMotorOperationalStatus_Stop;
                    /* power down the motor */
                    MotorControl(kLTMotorChannel_Pan,  kLTMotorCommmand_SetMode, kMotorDisablePowerDriver);
                    MotorControl(kLTMotorChannel_Tilt, kLTMotorCommmand_SetMode, kMotorDisablePowerDriver);
                    /* Reset the counter register after motor reset */
                    if (s_resetState) {
                        MotorControl(kLTMotorChannel_Pan,  kLTMotorCommmand_PulseRecH, 0);
                        MotorControl(kLTMotorChannel_Pan,  kLTMotorCommmand_PulseRecL, 0);
                        MotorControl(kLTMotorChannel_Tilt, kLTMotorCommmand_PulseRecH, 0);
                        MotorControl(kLTMotorChannel_Tilt, kLTMotorCommmand_PulseRecL, 0);
                        s_resetState = false;
                    }
                }
                for (u8 chn = 0; chn < kLTMotorChannel_PanTilt; chn++) {
                    u32 pulse = 0;
                    unsigned char Data = 0;
                    ReadRegister(kLTMotorCommmand_PulseRecH + kMotorConfigureTiltChannel * chn, &Data, sizeof(Data));
                    pulse = Data;
                    ReadRegister(kLTMotorCommmand_PulseRecL + kMotorConfigureTiltChannel * chn, &Data, sizeof(Data));
                    pulse = (((pulse << 8) & 0xff00) + (Data & 0xff));
                    if(chn ==  kLTMotorChannel_Pan) {
                        mData->x = (s32)pulse;
                    } else {
                        mData->y = (s32)pulse;
                    }
                }
                break;
            }
        case kMotorAction_MotorSpeed:
            {
                MotorPanTiltSpeed(*(u32 *)arg);
                break;
            }
        default:
            break;
    }
    return 0;
}

static bool ConfigureDeviceUnit(LTDeviceConfig *pDeviceConfig, u32 *pControlBusIndex, u32 deviceUnitSection) {
    const char *controlBusName;
    if (!(controlBusName = pDeviceConfig->ReadString(deviceUnitSection, "i2c-bus"))) {
        LTLOG_REDALERT("cdu.cbus.name", NULL);
        return false;
    }
    *pControlBusIndex = s_plibI2C->GetBusIndexFromName(controlBusName);
    if (*pControlBusIndex == LT_U32_MAX) {
        LTLOG_REDALERT("cdu.cbus.idx", "Invalid Bus name");
        return false;
    }
    if (!(s_hI2cBus = s_plibI2C->CreateDeviceUnitHandle(*pControlBusIndex))) {
        LTLOG_REDALERT("cdu.cbus.open", "Failed to open I2C bus %lu", LT_Pu32(*pControlBusIndex));
        return false;
    }
    if (!(s_plibI2C->SetDeviceConfiguration(s_hI2cBus, &(LTDeviceI2C_Configuration){.Async = false, .Master = true, .Frequency = MOTOR_IC_CLOCK, }))) {
        LTLOG_REDALERT("ins.cbus.cfg", "Failed to configure I2C bus");
        return false;
    }
    return true;
}

static bool ConfigureDeviceUnits(void) {
    LTDeviceConfig *pDeviceConfig = lt_openlibrary(LTDeviceConfig);
    if (!pDeviceConfig) {
        LTLOG_YELLOWALERT("lib.open.f", "LTDeviceConfig lib");
        return false;
    }
    u32 driverSection = pDeviceConfig->GetDriverSection("LTDeviceMotorPanTilt", "LTDriverMotorPanTiltMS320008N");
    s_DeviceUnits.nNumDeviceUnits = pDeviceConfig->GetNumDeviceUnits(driverSection);
    if (s_DeviceUnits.nNumDeviceUnits == 0) return true;
    s_DeviceUnits.pControlBusIndex = lt_malloc(s_DeviceUnits.nNumDeviceUnits * sizeof(u32));
    if (!s_DeviceUnits.pControlBusIndex) {
        LTLOG_REDALERT("cdus.oom", NULL);
        return false;
    }
    lt_memset(s_DeviceUnits.pControlBusIndex, 0, s_DeviceUnits.nNumDeviceUnits * sizeof(u32));
    for (u32 i = 0; i < s_DeviceUnits.nNumDeviceUnits; ++i) {
        u32 deviceUnitSection = pDeviceConfig->GetDeviceUnitSectionAt(driverSection, i);
        if (!deviceUnitSection) {
            LTLOG_YELLOWALERT("cdus.no", NULL);
            goto err;
        }
        if (!ConfigureDeviceUnit(pDeviceConfig, &s_DeviceUnits.pControlBusIndex[i], deviceUnitSection)) {
            LTLOG_REDALERT("cdus.err", "Failed to configure device unit %lu", LT_Pu32(i));
            goto err;
        }
    }
    lt_closelibrary(pDeviceConfig);
    return true;

err:
    lt_free(s_DeviceUnits.pControlBusIndex);
    s_DeviceUnits.pControlBusIndex = NULL;
    return false;
}


void *MotorOpen(void) {
    s_pdevPins = lt_openlibrary(LTDevicePins);
    if (!s_pdevPins) {
        LTLOG_REDALERT("init.fail.pins", "Failed to Init LTDevicePins");
        return NULL;
    }
    s_plibI2C = lt_openlibrary(LTDeviceI2C);
    if (!s_plibI2C) {
        LTLOG_REDALERT("init.fail.i2c", "Failed to Init Driver LTDeviceI2C");
        return NULL;
    }
    if (!ConfigureDeviceUnits()) return NULL;
    MotorControl(kLTMotorChannel_PanTilt, kLTMotorCommmand_Init, kMotorSetNonResetState);
    MotorControl(kLTMotorChannel_PanTilt, kLTMotorCommmand_ErrClr, kMotorClrUnderOverTemperature);
    return (void*)MotorCommandHandler;
}

void MotorClose(void) {
    lt_free(s_DeviceUnits.pControlBusIndex);
    s_DeviceUnits.pControlBusIndex = NULL;
    lt_closelibrary(s_plibI2C);
    lt_destroyhandle(s_hI2cBus);
    if (s_pdevPins) lt_closelibrary(s_pdevPins);
}
